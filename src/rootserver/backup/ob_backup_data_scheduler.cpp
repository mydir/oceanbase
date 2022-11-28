/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX RS

#include "ob_backup_data_scheduler.h"
#include "ob_backup_data_ls_task_mgr.h"
#include "ob_backup_data_set_task_mgr.h"
#include "ob_backup_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "rootserver/ob_root_utils.h"
#include "share/backup/ob_tenant_archive_mgr.h"
#include "share/backup/ob_backup_helper.h"
#include "rootserver/ob_server_manager.h"
#include "observer/ob_sql_client_decorator.h"
#include "share/ob_tenant_info_proxy.h"
#include "share/backup/ob_backup_connectivity.h"
#include "rootserver/ob_rs_event_history_table_operator.h"


namespace oceanbase
{
using namespace omt;
using namespace common::hash;
using namespace share;
namespace rootserver
{
ObBackupDataScheduler::ObBackupDataScheduler()
 : ObIBackupJobScheduler(BackupJobType::BACKUP_DATA_JOB),
   is_inited_(false),
   sql_proxy_(nullptr),
   rpc_proxy_(nullptr),
   schema_service_(nullptr),
   task_scheduler_(nullptr),
   lease_service_(nullptr),
   backup_service_(nullptr)
{
}

int ObBackupDataScheduler::init(
    common::ObMySQLProxy &sql_proxy,
    obrpc::ObSrvRpcProxy &rpc_proxy,
    share::schema::ObMultiVersionSchemaService &schema_service,
    ObBackupLeaseService  &lease_service, 
    ObBackupTaskScheduler &task_scheduler,
    ObBackupService &backup_service)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("[DATA_BACKUP]init twice", K(ret));
  } else {
    sql_proxy_ = &sql_proxy;
    rpc_proxy_ = &rpc_proxy;
    schema_service_ = &schema_service;
    task_scheduler_ = &task_scheduler;
    backup_service_ = &backup_service;
    lease_service_ = &lease_service;
    is_inited_ = true;
  }
  return ret;
}

int ObBackupDataScheduler::force_cancel(const uint64_t &tenant_id)
{
  int ret = OB_SUCCESS;
  ObSArray<uint64_t> empty_tenant_array;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[DATA_BACKUP]not init", K(ret));
  } else if (OB_FAIL(cancel_backup_data(tenant_id, empty_tenant_array))) {
    LOG_WARN("[DATA_BACKUP]failed to cancel backup data", K(ret));
  }
  return ret;
}

int ObBackupDataScheduler::get_need_reload_task(
    common::ObIAllocator &allocator,
    common::ObIArray<ObBackupScheduleTask *> &tasks)
{
  // 1. step1: get doing jobs from __all_backup_job;
  // 2. step1: get all peindg or doing ls tasks in __all_backup_ls_task from all doing jobs
  // 3. step2: add pending task into wait_list and add doing task into schedule_list
  int ret = OB_SUCCESS;
  bool for_update = false;
  ObArray<uint64_t> tenant_ids;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[DATA_BACKUP]not init", K(ret));
  } else if (OB_FAIL(get_all_normal_tenants_(tenant_ids))) {
    LOG_WARN("fail to get all normal tenants", K(ret));
  } else {
    ARRAY_FOREACH(tenant_ids, i) {
      const uint64_t tenant_id = tenant_ids.at(i);
      ObArray<ObBackupJobAttr> jobs;
      ObArray<ObBackupLSTaskAttr> ls_tasks;
      if (OB_FAIL(ObBackupJobOperator::get_jobs(*sql_proxy_, tenant_id, for_update, jobs))) {
        LOG_WARN("[DATA_BACKUP]failed to get backup jobs", K(ret), K(tenant_id));
      } else if (jobs.empty()) {
        LOG_INFO("[DATA_BACKUP]no job need to reload");
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < jobs.count(); ++i) {
          const ObBackupJobAttr &job = jobs.at(i);
          ObBackupSetTaskAttr set_task_attr;
          bool is_valid = true;
          if (OB_SYS_TENANT_ID == job.tenant_id_ 
              || ObBackupStatus::Status::DOING != job.status_.status_) { 
          } else if (OB_FAIL(ObBackupTaskOperator::get_backup_task(*sql_proxy_, job.job_id_, job.tenant_id_, set_task_attr))) {
            LOG_WARN("[DATA_BACKUP]failed to get set task", K(ret), K(job));
          } else if (OB_FAIL(ObBackupLSTaskOperator::get_ls_tasks(*sql_proxy_, job.job_id_, job.tenant_id_, for_update, ls_tasks))) {
            LOG_WARN("[DATA_BACKUP]failed to get ls tasks", K(ret), K(job));
          } else if (ls_tasks.empty()) { // no ls task, no need to reload
          } else if (OB_FAIL(do_get_need_reload_task_(job, set_task_attr, ls_tasks, allocator, tasks))) {
            LOG_WARN("[DATA_BACKUP]failed to reload ls task to scheduler", K(ret), K(job), K(ls_tasks));
          }
        }
      }
    }
  }
  return ret;
}

int ObBackupDataScheduler::do_get_need_reload_task_(
    const ObBackupJobAttr &job,
    const ObBackupSetTaskAttr &set_task_attr,
    const ObIArray<ObBackupLSTaskAttr> &ls_tasks,
    common::ObIAllocator &allocator,
    ObIArray<ObBackupScheduleTask *> &tasks)
{
  int ret = OB_SUCCESS;
  if (!job.is_valid() || ls_tasks.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(job), K(ls_tasks));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ls_tasks.count(); ++i) {
      const ObBackupLSTaskAttr &ls_task = ls_tasks.at(i);
      ObBackupScheduleTask *task = nullptr;
      if (OB_FAIL(build_task_(job, set_task_attr, ls_task, allocator, task))) {
        LOG_WARN("[DATA_BACKUP]failed to build task", K(ret), K(job), K(ls_task));
      } else if (OB_ISNULL(task)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("task is nullptr is unexpected", K(ret));
      } else if (ObBackupTaskStatus::Status::PENDING == ls_task.status_.status_
          || ObBackupTaskStatus::Status::DOING == ls_task.status_.status_) {
        if (OB_FAIL(tasks.push_back(task))) {
          LOG_WARN("[DATA_BACKUP]failed to push back task", K(ret), K(*task));
        }
      }
    }
  }
  return ret;
}

int ObBackupDataScheduler::build_task_(
    const ObBackupJobAttr &job,
    const ObBackupSetTaskAttr &set_task_attr,
    const ObBackupLSTaskAttr &ls_task,
    common::ObIAllocator &allocator,
    ObBackupScheduleTask *&task)
{
  int ret = OB_SUCCESS;
  switch (ls_task.task_type_.type_)
  {
  case ObBackupDataTaskType::Type::BACKUP_DATA_SYS:
  case ObBackupDataTaskType::Type::BACKUP_DATA_MINOR:
  case ObBackupDataTaskType::Type::BACKUP_DATA_MAJOR: {
    HEAP_VAR(ObBackupDataLSTask, tmp_task) {
      if (OB_FAIL(do_build_task_(job, set_task_attr, ls_task, allocator, tmp_task, task))) {
        LOG_WARN("[DATA_BACKUP]failed to do build task", K(ret), K(job), K(ls_task));
      }
    }
    break;
  }
  case ObBackupDataTaskType::Type::BACKUP_BUILD_INDEX: {
    HEAP_VAR(ObBackupBuildIndexTask, tmp_task) {
      if (OB_FAIL(do_build_task_(job, set_task_attr, ls_task, allocator, tmp_task, task))) {
        LOG_WARN("[DATA_BACKUP]failed to do build task", K(ret), K(job), K(ls_task));
      }
    }
    break;
  }
  case ObBackupDataTaskType::Type::BACKUP_PLUS_ARCHIVE_LOG: {
    HEAP_VAR(ObBackupComplLogTask, tmp_task) {
      if (OB_FAIL(do_build_task_(job, set_task_attr, ls_task, allocator, tmp_task, task))) {
        LOG_WARN("[DATA_BACKUP]failed to do build task", K(ret), K(job), K(ls_task));
      }
    }
    break;
  }
  case ObBackupDataTaskType::Type::BACKUP_META: {
    HEAP_VAR(ObBackupDataLSMetaTask, tmp_task) {
      if (OB_FAIL(do_build_task_(job, set_task_attr, ls_task, allocator, tmp_task, task))) {
        LOG_WARN("[DATA_BACKUP]failed to do build task", K(ret), K(job), K(ls_task));
      }
    }
    break;
  }
  default:
    break;
  }
  return ret;
}

template <typename T>
int ObBackupDataScheduler::do_build_task_(
    const ObBackupJobAttr &job,
    const ObBackupSetTaskAttr &set_task_attr,
    const ObBackupLSTaskAttr &ls_task,
    ObIAllocator &allocator,
    T &tmp_task,
    ObBackupScheduleTask *&task)
{
  int ret = OB_SUCCESS;
  int64_t task_deep_copy_size;
  void *raw_ptr = nullptr;
  if (OB_FAIL(tmp_task.build(job, set_task_attr, ls_task))) {
    LOG_WARN("[DATA_BACKUP]failed to build task", K(ret), K(job), K(ls_task));
  } else if (FALSE_IT(task_deep_copy_size = tmp_task.get_deep_copy_size())) {
  } else if (nullptr == (raw_ptr = allocator.alloc(task_deep_copy_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("[DATA_BACKUP]fail to allocate task", K(ret));
  } else if (OB_FAIL(tmp_task.clone(raw_ptr, task))) {
    LOG_WARN("[DATA_BACKUP]fail to clone input task", K(ret));
  } else if (nullptr == task) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[DATA_BACKUP]input task ptr is null", K(ret));
  }
  return ret;
}

int ObBackupDataScheduler::get_all_normal_tenants_(ObIArray<uint64_t> &tenants)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObArray<uint64_t> tmp_tenantid_array;
  if (OB_FAIL(schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
    LOG_WARN("failed to get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_ids(tmp_tenantid_array))) {
    LOG_WARN("failed to get tenant ids", K(ret));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < tmp_tenantid_array.count(); ++i) {
    const uint64_t tenant_id = tmp_tenantid_array.at(i);
    if (is_user_tenant(tenant_id)) {
    } else {
      const ObTenantSchema *meta_tenant_info = nullptr;
      const ObTenantSchema *user_tenant_info = nullptr;
      uint64_t user_tenant_id = gen_user_tenant_id(tenant_id);
      if (OB_FAIL(schema_guard.get_tenant_info(tenant_id, meta_tenant_info))) {
        LOG_WARN("failed to get meta tenant info", K(ret), K(tenant_id));
      } else if (OB_ISNULL(meta_tenant_info)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("meta tenant info must not be nullptr", K(ret));
      } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_id, user_tenant_info))) {
        LOG_WARN("failed to get meta tenant info", K(ret), K(tenant_id));
      } else if (OB_ISNULL(user_tenant_info)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("meta tenant info must not be nullptr", K(ret));
      } else if (meta_tenant_info->is_normal() && user_tenant_info->is_normal()) {
        if (OB_FAIL(tenants.push_back(tenant_id))) {
          LOG_WARN("failed to push back tenant id", K(ret), K(tenant_id));
        }
      }
    }
  }
  return ret;
}

int ObBackupDataScheduler::cancel_backup_data(
    const uint64_t tenant_id,
    const common::ObIArray<uint64_t> &backup_tenant_ids)
{
  // Three cancellation strategies:
  // 1. if tenant_id is sys tenant and backup tenant ids is empty, cancel all the backup jobs of all the tenants
  // 2. if tenant_id is sys tenant and backup tenant ids isn't empty, cancel all the backup jobs of tenants in backup tenant ids
  // 3. if tenant_id is user tenant, cancel all backup jobs of tenant id.
  int ret = OB_SUCCESS;
  ObSArray<uint64_t> need_cancel_backup_tenants;
  bool is_valid = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[DATA_BACKUP]backup data scheduler not init", K(ret));
  } else if (OB_FAIL(get_need_cancel_tenants_(tenant_id, backup_tenant_ids, need_cancel_backup_tenants))) {
    LOG_WARN("[DATA_BACKUP]failed to get need cancel backup tenants", K(ret), K(tenant_id), K(backup_tenant_ids));
  } else {
    for (int64_t i =0; OB_SUCC(ret) && i < need_cancel_backup_tenants.count(); ++i) {
      const uint64_t need_cancel_backup_tenant = need_cancel_backup_tenants.at(i);
      if (!is_valid_tenant_id(need_cancel_backup_tenant)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("[DATA_BACKUP]invalid tenant", K(ret), K(need_cancel_backup_tenant));
      } else if (OB_FAIL(check_tenant_status(*schema_service_, need_cancel_backup_tenant, is_valid))) {
      } else if (!is_valid) { 
        LOG_INFO("tenant status not normal, no need to schedule backup", K(need_cancel_backup_tenant));
      } else if (OB_FAIL(lease_service_->check_lease())) {
        LOG_WARN("fail to check leader", K(ret));
      } else if (OB_FAIL(ObBackupJobOperator::cancel_jobs(*sql_proxy_, need_cancel_backup_tenant))) {
        LOG_WARN("fail to cancel backup jobs", K(ret), K(need_cancel_backup_tenant));
      } 
    }
  }
  ROOTSERVICE_EVENT_ADD("backup_data", "cancel_backup_data", K(tenant_id), K(backup_tenant_ids));
  return ret;
}

int ObBackupDataScheduler::get_need_cancel_tenants_(const uint64_t tenant_id, 
    const common::ObIArray<uint64_t> &backup_tenant_ids, common::ObIArray<uint64_t> &need_cancel_backup_tenants)
{
  int ret = OB_SUCCESS;
  need_cancel_backup_tenants.reset();
  if (is_sys_tenant(tenant_id) && backup_tenant_ids.empty()) {
    if (OB_FAIL(schema_service_->get_tenant_ids(need_cancel_backup_tenants))) {
      LOG_WARN("fail to get all tenants", K(ret));
    }
  } else if (is_sys_tenant(tenant_id) && !backup_tenant_ids.empty()) {
    if (OB_FAIL(need_cancel_backup_tenants.assign(backup_tenant_ids))) {
      LOG_WARN("fail to assign backup tenant ids", K(backup_tenant_ids));
    }
  } else if (is_user_tenant(tenant_id) && backup_tenant_ids.empty()) {
    if (OB_FAIL(need_cancel_backup_tenants.push_back(tenant_id))) {
      LOG_WARN("fail to push backup tenant", K(ret), K(tenant_id));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_id or backup tenant ids", K(ret), K(tenant_id), K(backup_tenant_ids));
  }
  return ret;
}

int ObBackupDataScheduler::start_backup_data(const obrpc::ObBackupDatabaseArg &in_arg)
{
  int ret = OB_SUCCESS;
  ObBackupJobAttr template_job_attr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[DATA_BACKUP]backup data is not init", K(ret));
  } else if (!in_arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(in_arg));
  } else if (OB_FAIL(fill_template_job_(in_arg, template_job_attr))) {
    LOG_WARN("[DATA_BACKUP]failed to fill backup base arg", K(ret), K(in_arg));
  } else if (OB_SYS_TENANT_ID == in_arg.tenant_id_) {
    // backup databse initiate by sys tenant
    // if backup_tenant_ids is empty, backup all tenants, else backup tenant which is only in backup_tenant_ids.
    if (OB_FAIL(start_sys_backup_data_(template_job_attr))) {
      LOG_WARN("[DATA_BACKUP]failed to start cluster backup", K(ret), K(in_arg));
    }
  } else {
    // backup databse initiate by ordinary tenant
    if (!in_arg.backup_tenant_ids_.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("[DATA_BACKUP]tenant backup should not initiate another tenant", K(ret), K(in_arg));
    } else if (OB_FAIL(start_tenant_backup_data_(template_job_attr))) {
      LOG_WARN("[DATA_BACKUP]failed to start tenant backup", K(ret), K(in_arg));
    }
  }

  if (OB_SUCC(ret)) {
    backup_service_->wakeup();
    FLOG_INFO("[DATA_BACKUP]insert backup data job succeed", K(in_arg));
  }
  ROOTSERVICE_EVENT_ADD("backup_data", "start_backup_data", "tenant_id", in_arg.tenant_id_, "result", ret);
  return ret;
}

int ObBackupDataScheduler::fill_template_job_(const obrpc::ObBackupDatabaseArg &in_arg, ObBackupJobAttr &job_attr)
{
  int ret = OB_SUCCESS;
  int64_t incarnation_id = 1;
  if (!in_arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(in_arg));
  } else if (in_arg.is_compl_log_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("backup with plus archive is not supported", K(ret), K(in_arg));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "plus archive is not supported");
  } else if (!in_arg.backup_dest_.is_empty()) {
    ObBackupDest dest;
    if (OB_FAIL(dest.set(in_arg.backup_dest_.ptr()))) {
      LOG_WARN("[DATA_BACKUP]failed to set dest", K(ret));
    } else if (OB_FAIL(dest.get_backup_path_str(job_attr.backup_path_.ptr(), job_attr.backup_path_.capacity()))) {
      LOG_WARN("[DATA_BACKUP]failed to get backup path str", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(job_attr.passwd_.assign(in_arg.passwd_))) {
    LOG_WARN("[DATA_BACKUP]failed to assgin passwd_", K(ret));
  } else if (OB_FAIL(job_attr.description_.assign(in_arg.backup_description_))) {
    LOG_WARN("[DATA_BACKUP]failed to assgin description", K(ret));
  } else if (OB_FAIL(job_attr.executor_tenant_id_.assign(in_arg.backup_tenant_ids_))) {
    LOG_WARN("[DATA_BACKUP]failed to assgin backup tenant id", K(ret));
  } else {
    job_attr.tenant_id_ = in_arg.tenant_id_;
    job_attr.incarnation_id_ = incarnation_id;
    job_attr.initiator_tenant_id_ = in_arg.initiator_tenant_id_;
    job_attr.initiator_job_id_ = in_arg.initiator_job_id_;
    job_attr.plus_archivelog_ = in_arg.is_compl_log_;
    job_attr.start_ts_ = ObTimeUtility::current_time();
    job_attr.backup_type_.type_ = in_arg.is_incremental_ ?
        ObBackupType::BackupType::INCREMENTAL_BACKUP : ObBackupType::BackupType::FULL_BACKUP;
    job_attr.encryption_mode_ = in_arg.encryption_mode_;
    job_attr.status_.status_ = ObBackupStatus::Status::INIT;
    job_attr.end_ts_ = 0;
    job_attr.result_ = OB_SUCCESS;
    if (OB_SYS_TENANT_ID == in_arg.tenant_id_) {
      job_attr.backup_level_.level_ = ObBackupLevel::Level::CLUSTER;
    } else {
      job_attr.backup_level_.level_ = ObBackupLevel::Level::USER_TENANT;
    }
  }
  return ret;
}

int ObBackupDataScheduler::get_all_tenants_(const bool with_backup_dest, ObIArray<uint64_t> &tenants)
{
  int ret = OB_SUCCESS;
  ObArray<uint64_t> tmp_tenants;
  if (OB_FAIL(ObTenantUtils::get_tenant_ids(schema_service_, tmp_tenants))) {
    LOG_WARN("[DATA_BACKUP]failed to get all tenants id", K(ret));
  } else {
    int tmp_ret = OB_SUCCESS;
    for (int64_t i = 0; i < tmp_tenants.count(); ++i ) {
      const uint64_t tenant_id = tmp_tenants.at(i);
      bool is_valid = true;
      bool is_doing = true;
      bool is_setted = true;
      tmp_ret = OB_SUCCESS;
      if (!is_user_tenant(tenant_id) || OB_SYS_TENANT_ID == tenant_id) {
      } else if (OB_SUCCESS != (tmp_ret = check_tenant_status(*schema_service_, tenant_id, is_valid))) {
        LOG_WARN("[DATA_BACKUP]failed to check can do backup", K(tmp_ret), K(tenant_id));
      } else if (!is_valid) {
        LOG_INFO("[DATA_BACKUP]tenant status not valid, no need to backup", K(tenant_id));
      } else if (OB_SUCCESS != (tmp_ret = check_log_archive_status_(tenant_id, is_doing))) {
        LOG_WARN("[DATA_BACKUP]failed to check log archive status", K(tmp_ret), K(tenant_id));
      } else if (!is_doing) {
        LOG_INFO("log archive is not doing, no need to backup", K(tenant_id));
      } else if (OB_SUCCESS != (tmp_ret = check_initiate_twice_by_same_tenant_(tenant_id, OB_SYS_TENANT_ID))) {
        LOG_WARN("[DATA_BACKUP]failed to check initiate twice by same tenant", K(tmp_ret), K(tenant_id));
      } else if (!with_backup_dest && OB_SUCCESS != (tmp_ret =  check_tenant_set_backup_dest_(tenant_id, is_setted))) {
        LOG_WARN("fail to check backup dest is valid", K(ret), K(tenant_id));
      } else if (!is_setted) {
        LOG_INFO("backup dest is not setted, can't start backup", K(ret), K(tenant_id));
      } else if (OB_FAIL(tenants.push_back(tenant_id))) {
        LOG_WARN("[DATA_BACKUP]failed to push back tenant", K(ret));
      }
    }

    if (OB_SUCC(ret) && tenants.empty()) {
      ret = OB_BACKUP_CAN_NOT_START;
      LOG_WARN("[DATA_BACKUP]can't start cluster backup with no tenant", K(ret));
      LOG_USER_ERROR(OB_BACKUP_CAN_NOT_START, "no tenant meets the backup condition, ￼" 
        "please check the tenant status, log archive status, backup data dest, and backup status for each tenant");
    }
  }
  return ret;
}

int ObBackupDataScheduler::check_log_archive_status_(const uint64_t tenant_id, bool &is_doing)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTenantArchiveRoundAttr round_attr;
  const int64_t ERROR_MSG_LENGTH = 1024;
  char error_msg[ERROR_MSG_LENGTH] = "";
  int64_t pos = 0;
  int64_t fake_incarnation = 1;
  is_doing = false;
  if (OB_FAIL(ObTenantArchiveMgr::get_tenant_current_round(tenant_id, fake_incarnation, round_attr))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("[DATA_BACKUP]failed to get cur log archive round", K(ret));
    }
  } else if (ObArchiveRoundState::Status::DOING == round_attr.state_.status_) {
    is_doing = true;
  }
  return ret;
}

int ObBackupDataScheduler::check_tenant_set_backup_dest_(const uint64_t tenant_id, bool &is_setted)
{
  int ret = OB_SUCCESS;
  share::ObBackupHelper backup_helper;
  ObBackupPathString backup_dest_str;
  is_setted = false;
  if (OB_FAIL(backup_helper.init(tenant_id, *sql_proxy_))) {
    LOG_WARN("fail to init backup help", K(ret));
  } else if (OB_FAIL(backup_helper.get_backup_dest(backup_dest_str))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get backup dest", K(ret), K(tenant_id));
    }
  } else if (!backup_dest_str.is_empty()) {
    is_setted = true;
  } 
  return ret;
}

int ObBackupDataScheduler::start_sys_backup_data_(const ObBackupJobAttr &job_attr)
{
  int ret = OB_SUCCESS;
  ObBackupJobAttr new_job_attr;
  if (!job_attr.is_tmplate_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(job_attr));
  } else if (OB_FAIL(new_job_attr.assign(job_attr))) {
    LOG_WARN("[DATA_BACKUP]failed to assign job attr", K(ret), K(job_attr));
  } else if (!job_attr.executor_tenant_id_.empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < job_attr.executor_tenant_id_.count(); ++i) {
      const uint64_t tenant_id = job_attr.executor_tenant_id_.at(i);
      bool is_valid = true;
      bool is_doing = true;
      if (OB_FAIL(check_tenant_status(*schema_service_, tenant_id, is_valid))) {
        LOG_WARN("[DATA_BACKUP]failed to check can do backup", K(ret), K(tenant_id));
      } else if (!is_valid) {
        ret = OB_BACKUP_CAN_NOT_START;
        LOG_WARN("[DATA_BACKUP]tenant status are not valid, can't start backup", K(ret), K(tenant_id));
        LOG_USER_ERROR(OB_BACKUP_CAN_NOT_START, "tenant status is not normal.");
      } else if (OB_FAIL(check_log_archive_status_(tenant_id, is_doing))) {
        LOG_WARN("[DATA_BACKUP]failed to check log archive status", K(ret), K(tenant_id));
      } else if (!is_doing) {
        ret = OB_BACKUP_CAN_NOT_START;
        LOG_WARN("log archive status is not doing, can't start backup", K(ret), K(tenant_id));
        LOG_USER_ERROR(OB_BACKUP_CAN_NOT_START, "log archive status is not doing.");
      } else if (OB_FAIL(check_initiate_twice_by_same_tenant_(tenant_id, new_job_attr.initiator_tenant_id_))) {
        LOG_WARN("[DATA_BACKUP]failed to check initiate twice by same tenant", K(ret), K(tenant_id));
      } else if (job_attr.backup_path_.is_empty()) {
        bool is_setted = true;
        if(OB_FAIL(check_tenant_set_backup_dest_(tenant_id, is_setted))) {
          LOG_WARN("fail to check backup dest valid", K(ret));
        } else if (!is_setted) {
          ret = OB_BACKUP_CAN_NOT_START;
          LOG_WARN("backup dest is not setted, can't start backup", K(ret), K(tenant_id));
          LOG_USER_ERROR(OB_BACKUP_CAN_NOT_START, "backup dest is not set.");
        }
      } 
    }
  } else if (OB_FAIL(get_all_tenants_(job_attr.backup_path_.is_empty(), new_job_attr.executor_tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to get all tenants", K(ret));
  }
  
  if (OB_SUCC(ret)) {
    ObMySQLTransaction trans;
    if (OB_FAIL(trans.start(sql_proxy_, OB_SYS_TENANT_ID))) {
      LOG_WARN("[DATA_BACKUP]failed to start trans", K(ret), K(new_job_attr));
    } else {
      if (OB_FAIL(get_next_job_id(trans, new_job_attr.tenant_id_, new_job_attr.job_id_))) {
        LOG_WARN("[DATA_BACKUP]failed to get next job id", K(ret));
      } else if (OB_FALSE_IT(new_job_attr.initiator_job_id_ = new_job_attr.job_id_)) {
      } else if (OB_FAIL(lease_service_->check_lease())) {
        LOG_WARN("fail to check leader", K(ret));
      } else if (OB_FAIL(ObBackupJobOperator::insert_job(trans, new_job_attr))) {
        LOG_WARN("[DATA_BACKUP]failed to create new backup job", K(ret), K(new_job_attr));
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(trans.end(true))) {
          LOG_WARN("[DATA_BACKUP]failed to commit trans", KR(ret));
        }
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
          LOG_WARN("[DATA_BACKUP]failed to rollback", KR(ret), K(tmp_ret));
        }
      }
    }
  }
  return ret;
}

int ObBackupDataScheduler::start_tenant_backup_data_(const ObBackupJobAttr &job_attr)
{
  int ret = OB_SUCCESS;
  bool is_valid = true;
  ObBackupJobAttr new_job_attr;
  ObBackupPathString backup_path;
  bool is_doing = true;
  if (!job_attr.is_tmplate_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid tmplate job", K(ret), K(job_attr));
  } else if (OB_FAIL(new_job_attr.assign(job_attr))) {
    LOG_WARN("[DATA_BACKUP]failed to assign job_attr", K(ret), K(job_attr));
  } else if (OB_FAIL(get_backup_path(*sql_proxy_, new_job_attr.tenant_id_, backup_path))) {
    LOG_WARN("[DATA_BACKUP]failed to get backup dest", K(ret), K(new_job_attr));
  } else if (new_job_attr.backup_path_.is_empty() && OB_FAIL(new_job_attr.backup_path_.assign(backup_path))) {
    LOG_WARN("[DATA_BACKUP]failed to assign backup path", K(ret), K(backup_path));
  } else if (OB_FAIL(check_tenant_status(*schema_service_, new_job_attr.tenant_id_, is_valid))) {
    LOG_WARN("[DATA_BACKUP]failed to check tenant can do backup", K(ret), K(new_job_attr));
  } else if (!is_valid) {
    ret = OB_BACKUP_CAN_NOT_START;
    LOG_USER_ERROR(OB_BACKUP_CAN_NOT_START, "tenant status is not normal.");
    LOG_WARN("[DATA_BACKUP]tenant status is not valid, can't start backup", K(ret), K(new_job_attr));
  } else if (OB_FAIL(check_initiate_twice_by_same_tenant_(new_job_attr.tenant_id_, new_job_attr.initiator_tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to check initiate twice", K(ret), K(new_job_attr));
  } else if (OB_FAIL(check_log_archive_status_(new_job_attr.tenant_id_, is_doing))) {
    LOG_WARN("[DATA_BACKUP]failed to check log archive status", K(ret), K(new_job_attr));
  } else if (!is_doing) {
   ret = OB_BACKUP_CAN_NOT_START;
   LOG_USER_ERROR(OB_BACKUP_CAN_NOT_START, "log archive status is not doing.");
   LOG_WARN("log archive is not doing, can't start backup", K(ret));
  } else {
    ObMySQLTransaction trans;
    if (OB_FAIL(trans.start(sql_proxy_, gen_meta_tenant_id(new_job_attr.tenant_id_)))) {
      LOG_WARN("[DATA_BACKUP]failed to start trans", K(ret), K(job_attr));
    } else {
      if (OB_FAIL(get_next_job_id(trans, new_job_attr.tenant_id_, new_job_attr.job_id_))) {
        LOG_WARN("[DATA_BACKUP]failed to get next job id", K(ret));
      } else if (OB_FAIL(get_next_backup_set_id(trans, new_job_attr.tenant_id_, new_job_attr.backup_set_id_))) {
        LOG_WARN("[DATA_BACKUP]failed to get next backup set id", K(ret));
      } else if (OB_FAIL(update_backup_type_(trans, new_job_attr.tenant_id_, new_job_attr.backup_set_id_, 
          new_job_attr.backup_path_, new_job_attr.backup_type_))) {
        LOG_WARN("[DATA_BACKUP]failed to update backup type", K(ret), K(new_job_attr));
      } else if (OB_FAIL(new_job_attr.executor_tenant_id_.push_back(new_job_attr.tenant_id_))) {
        LOG_WARN("[DATA_BACKUP]failed to push back tenant id", K(ret));
      } else if (OB_FALSE_IT(new_job_attr.initiator_job_id_ = new_job_attr.tenant_id_ == new_job_attr.initiator_tenant_id_ ?
          0/*no parent job*/ : new_job_attr.initiator_job_id_)) {
      } else if (OB_FAIL(lease_service_->check_lease())) {
        LOG_WARN("fail to check leader", K(ret));
      } else if (OB_FAIL(ObBackupJobOperator::insert_job(trans, new_job_attr))) {
        LOG_WARN("[DATA_BACKUP]failed to create new backup job", K(ret), K(job_attr));
      } else {
        LOG_INFO("succeed insert user tenant backup job", K(new_job_attr));
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(trans.end(true))) {
          LOG_WARN("[DATA_BACKUP]failed to commit trans", KR(ret));
        }
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
          LOG_WARN("[DATA_BACKUP]failed to rollback", KR(ret), K(tmp_ret));
        }
      }
    }
  }
  return ret;
}

int ObBackupDataScheduler::get_scn(common::ObISQLClient &sql_proxy, const uint64_t tenant_id, palf::SCN &scn)
{
  int ret = OB_SUCCESS;
  ObAllTenantInfo tenant_info;
  const bool for_update = false;
  if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(tenant_id, &sql_proxy, for_update, tenant_info))) {
    LOG_WARN("failed to get tenant info", K(ret), K(tenant_id));
  } else {
    // TODO when yaoying provide the scn_to_str, remove this .
    // The conversion accuracy of SCN to time_stamp is inconsistent under MySQL mode and Oracle mode.
    // The conversion accuracy in ORALCE mode is nanosecond, but it is microsecond in mysql
    // for backup and restore, we keep the scn round up to microseconds that keep the conversion accuracy is consistent.
    // meanwhile, in order to solve that boundary is not included in the restore, scn + 1;
    // 1658475549197665190 --> 1658475549197666000
    palf::SCN tmp_scn;
    int64_t ts;
    ts = tenant_info.get_standby_scn().convert_to_ts();
    if (OB_FAIL(tmp_scn.convert_from_ts(ts))) {
      LOG_WARN("fail to convert from ts", K(ret), K(ts));
    } else if (tenant_info.get_standby_scn() != tmp_scn && OB_FAIL(tmp_scn.convert_from_ts(ts + 1))) {
      LOG_WARN("fail to convert from ts", K(ret), K(ts));
    } else {
      scn = tmp_scn;
    }
  }
  return ret;
}

int ObBackupDataScheduler::check_tenant_status(
    share::schema::ObMultiVersionSchemaService &schema_service,
    uint64_t tenant_id,
    bool &is_valid)
{
  int ret = OB_SUCCESS;
  bool is_dropped = false;
  is_valid = false;
  ObSchemaGetterGuard schema_guard;
  const ObSimpleTenantSchema *tenant_schema = nullptr;

  if (OB_FAIL(schema_service.check_if_tenant_has_been_dropped(tenant_id, is_dropped))) {
    LOG_WARN("[DATA_BACKUP]failed to check if tenant has been dropped", K(ret), K(tenant_id));
  } else if (is_dropped) {
    is_valid = false;
    LOG_WARN("[DATA_BACKUP]tenant is dropped, can't not backup now", K(tenant_id));
  } else if (OB_FAIL(schema_service.get_tenant_schema_guard(tenant_id, schema_guard))) {
    LOG_WARN("[DATA_BACKUP]failed to get schema guard", K(ret), K(tenant_id));
  } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_id, tenant_schema))) {
    LOG_WARN("[DATA_BACKUP]failed to get tenant info", K(ret), K(tenant_id));
  } else if (tenant_schema->is_normal()) {
    is_valid = true;
  } else if (tenant_schema->is_creating()) {
    is_valid = false;
    LOG_WARN("[DATA_BACKUP]tenant is creating, can't not backup now", K(tenant_id));
  } else if (tenant_schema->is_restore()) {
    is_valid = false;
    LOG_WARN("[DATA_BACKUP]tenant is doing restore, can't not backup now", K(tenant_id));
  } else if (tenant_schema->is_dropping()) {
    is_valid = false;
    LOG_WARN("[DATA_BACKUP]tenant is dropping, can't not backup now", K(tenant_id));
  } else if (tenant_schema->is_in_recyclebin()) {
    is_valid = false;
    LOG_WARN("[DATA_BACKUP]tenant is in recyclebin, can't not backup now", K(tenant_id));
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[DATA_BACKUP]unknown tenant status", K(tenant_id), K(tenant_schema));
  }
  return ret;
}

int ObBackupDataScheduler::check_initiate_twice_by_same_tenant_(const uint64_t tenant_id, const uint64_t initiator_tenant_id)
{
  int ret = OB_SUCCESS;
  int64_t cnt = 0;
  if (OB_FAIL(ObBackupJobOperator::cnt_jobs(*sql_proxy_, tenant_id, initiator_tenant_id, cnt))) {
    LOG_WARN("[DATA_BACKUP]failed to cnt jobs", K(ret), K(tenant_id), K(initiator_tenant_id));
  } else if (cnt >= 1) {
    ret = OB_BACKUP_IN_PROGRESS;
    LOG_WARN("[DATA_BACKUP]the same tenant can't be initiated twice by the same tenant", K(ret), K(tenant_id), K(initiator_tenant_id));
  }
  return ret;
}

int ObBackupDataScheduler::get_next_job_id(common::ObISQLClient &trans, const uint64_t tenant_id, int64_t &job_id)
{
  int ret = OB_SUCCESS;
  job_id = -1;
  if (OB_INVALID_TENANT_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id", K(ret), K(tenant_id));
  } else if (OB_FAIL(ObLSBackupInfoOperator::get_next_job_id(trans, tenant_id, job_id))) {
    LOG_WARN("[DATA_BACKUP]failed to get next job id", K(ret), K(tenant_id));
  } else if (-1 == job_id) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[DATA_BACKUP]get invalid job id", K(ret));
  }
  return ret;
}

int ObBackupDataScheduler::get_next_backup_set_id(common::ObISQLClient &trans, const uint64_t tenant_id, 
    int64_t &next_backup_set_id) 
{
  int ret = OB_SUCCESS;
  next_backup_set_id = -1;
  if (OB_FAIL(ObLSBackupInfoOperator::get_next_backup_set_id(trans, tenant_id, next_backup_set_id))) {
    LOG_WARN("fail to get next backup set id", K(ret), K(tenant_id));
  } 
  return ret;
}

int ObBackupDataScheduler::update_backup_type_(common::ObISQLClient &trans, const uint64_t tenant_id, 
    const int64_t backup_set_id, const share::ObBackupPathString &backup_path, share::ObBackupType &backup_type)
{
  // if backup type is inc backup but no prev backup set id.
  // adjust the backup type from inc backup to full backup.
  int ret = OB_SUCCESS;
  int64_t prev_full_backup_set_id = -1;
  int64_t pre_inc_backup_set_id = -1;
  if (OB_FAIL(ObBackupSetFileOperator::get_prev_backup_set_id(
      trans, tenant_id, backup_set_id, backup_type, backup_path, prev_full_backup_set_id, pre_inc_backup_set_id))) {
    if (OB_ENTRY_NOT_EXIST == ret && backup_type.is_inc_backup()) {
      backup_type.type_ = ObBackupType::BackupType::FULL_BACKUP;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get prev backup set id", K(ret), K(tenant_id), K(backup_set_id));
    }
  } 
  return ret;
}

int ObBackupDataScheduler::get_backup_path(common::ObISQLClient &sql_proxy, const uint64_t tenant_id, 
    ObBackupPathString &backup_path)
{
  int ret = OB_SUCCESS;
  share::ObBackupHelper backup_helper;
  ObBackupPathString backup_dest_str;
  ObBackupDest dest;
  if (OB_FAIL(backup_helper.init(tenant_id, sql_proxy))) {
    LOG_WARN("fail to init backup help", K(ret));
  } else if (OB_FAIL(backup_helper.get_backup_dest(backup_dest_str))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_BACKUP_CAN_NOT_START;
      LOG_WARN("backup data dest is not set", K(ret));
    } else {
      LOG_WARN("fail to get backup dest", K(ret), K(tenant_id));
    }
  } else if (backup_dest_str.is_empty()) {
    ret = OB_BACKUP_CAN_NOT_START;
    LOG_WARN("empty backup dest is not allowed, backup can't start", K(ret));
  } else if (OB_FAIL(dest.set(backup_dest_str.ptr()))) {
    LOG_WARN("fail to set backup dest", K(ret), K(tenant_id));
  } else if (OB_FAIL(dest.get_backup_path_str(backup_path.ptr(), backup_path.capacity()))) {
    LOG_WARN("fail to get backup path str", K(ret), K(tenant_id));
  }
  return ret;
}

int ObBackupDataScheduler::handle_execute_over(
    const ObBackupScheduleTask *task,
    bool &can_remove,
    const ObAddr &black_server,
    const int execute_ret)
{
  //cases of call handle_execute_over
  //1. observer return a rpc to tell task scheduler task finish (success or fail)
  //2. task scheduler find a task not on the dest observer
  //3. task scheduler do execute failed
  int ret = OB_SUCCESS;
  can_remove = false;
  bool is_valid = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[DATA_BACKUP]not init", K(ret));
  } else if (nullptr == task) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret));
  } else if (OB_FAIL(ObBackupDataScheduler::check_tenant_status(*schema_service_, task->get_tenant_id(), is_valid))) {
    LOG_WARN("fail to check tenant status", K(ret));
  } else if (!is_valid) {
    can_remove = false;
  } else {
    // first get task from __all_backup_log_stream_task
    ObBackupLSTaskAttr ls_attr;
    ObMySQLTransaction trans;
    ObLSID ls_id(task->get_ls_id());
    if (OB_FAIL(trans.start(sql_proxy_, gen_meta_tenant_id(task->get_tenant_id())))) {
      LOG_WARN("[DATA_BACKUP]failed to start trans", K(ret));
    } else {
      if (OB_FAIL(ObBackupLSTaskOperator::get_ls_task(trans, true/*for update*/, task->get_task_id(), task->get_tenant_id(), ls_id, ls_attr))) {
        LOG_WARN("[DATA_BACKUP]failed to get log stream task", K(ret), K(*task));
      } else if (OB_FAIL(ObBackupDataLSTaskMgr::statistic_info(*lease_service_, trans, ls_attr))) { 
  // TODO: delete this when observer support the function that backup progress statistics
        LOG_WARN("[DATA_BACKUP]failed to update statistic infomation of ls task", K(ret), K(ls_attr));
      } else if (OB_FALSE_IT(ls_attr.end_ts_ = ObTimeUtility::current_time())) {
      } else if (ObBackupTaskStatus::Status::DOING == ls_attr.status_.status_) {
        ObBackupTaskStatus next_status(ObBackupTaskStatus::Status::FINISH);
        if (OB_SUCCESS != execute_ret && OB_FAIL(ObBackupDataLSTaskMgr::update_black_server(*lease_service_, trans, ls_attr, black_server))) {
           LOG_WARN("[DATA_BACKUP]failed to update black server", K(ret), K(ls_attr), K(black_server));
        } else if (OB_FAIL(ObBackupDataLSTaskMgr::advance_status(*lease_service_, trans, ls_attr, next_status, execute_ret, ls_attr.end_ts_))) {
          LOG_WARN("[DATA_BACKUP]failed to advance status", K(ret), K(ls_attr), K(next_status), K(execute_ret));
        } 
      } else {
        LOG_WARN("concurrent scenario! this task will need reload to redo.", K(ls_attr));
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(trans.end(true))) {
          LOG_WARN("[DATA_BACKUP]failed to commit trans", K(ret));
        } else {
          can_remove = true;
          backup_service_->wakeup();
          LOG_INFO("succeed handle execute over ls task.", K(ls_attr));
        }
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
          LOG_WARN("[DATA_BACKUP]failed to rollback trans", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObBackupDataScheduler::handle_failed_job_(
    const uint64_t tenant_id,
    const int64_t result,
    ObIBackupJobMgr &job_mgr,
    ObBackupJobAttr &job_attr)
{
  int ret = OB_SUCCESS;
  if (OB_SYS_TENANT_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sys tenant does not handle failed", K(ret), K(tenant_id));
  } else if (!job_mgr.is_can_retry(result) || job_attr.retry_count_ >= OB_MAX_RETRY_TIMES) {
    if (OB_FAIL(job_mgr.deal_non_reentrant_job(result))) {
      LOG_WARN("failed to deal failed job", K(ret), K(job_attr));
    }
  } else {
    job_attr.retry_count_++;
    if (OB_FAIL(ObBackupJobOperator::update_retry_count(*sql_proxy_, job_attr))) {
      LOG_WARN("failed to persist retry times", K(ret), K(job_attr));
    } else {
      backup_service_->wakeup();
    }
  }
  return ret;
} 

int ObBackupDataScheduler::process()
{

  // TODO: remove get_all_normal_tenants  when adjust tenant level thread.

  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObIBackupJobMgr *job_mgr = nullptr;
  ObArray<uint64_t> tenant_ids;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[DATA_BACKUP]backup up data scheduler not init", K(ret));
  } else if (OB_FAIL(get_all_normal_tenants_(tenant_ids))) { 
    LOG_WARN("fail to get all normal tenants", K(ret));
  } else {
    ARRAY_FOREACH(tenant_ids, i) {
      const uint64_t tenant_id = tenant_ids.at(i);
      ObArray<ObBackupJobAttr> backup_jobs;
      if (OB_FAIL(ObBackupJobOperator::get_jobs(*sql_proxy_, tenant_id, false/*not for update*/, backup_jobs))) {
        LOG_WARN("[DATA_BACKUP]failed to get backup jobs", K(ret));
      } else if (backup_jobs.empty()) {
      } else {
        if (OB_FAIL(ObBackupJobMgrAlloctor::alloc(tenant_id, job_mgr))) {
          LOG_WARN("fail to alloc job mgr", K(ret), K(tenant_id));
        } else if (OB_ISNULL(job_mgr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("backup job mgr can't be nullptr", K(ret), K(tenant_id));
        } 
        for (int64_t i = 0; OB_SUCC(ret) && i < backup_jobs.count(); ++i) {
          ObBackupJobAttr &job_attr = backup_jobs.at(i);
          job_mgr->reset();
          if (!job_attr.is_valid()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("[DATA_BACKUP]backup job is not valid", K(ret), K(job_attr));
          } else if (OB_FAIL(job_mgr->init(tenant_id, job_attr, *sql_proxy_, *rpc_proxy_, 
              *task_scheduler_, *lease_service_, *schema_service_, *backup_service_))) {
            LOG_WARN("[DATA_BACKUP]failed to init tenant backup job mgr", K(ret), K(tenant_id), K(job_attr));
          } else if (OB_SUCCESS != (tmp_ret = job_mgr->process())) { // tenant level backups are isolated
            LOG_WARN("[DATA_BACKUP]failed to schedule tenant backup job", K(tmp_ret), K(tenant_id), K(job_attr));
            if (!is_sys_tenant(tenant_id) && OB_SUCCESS != (tmp_ret = handle_failed_job_(tenant_id, tmp_ret, *job_mgr, job_attr))) {
              LOG_WARN("failed to handle user tenant failed job", K(tmp_ret), K(job_attr));
            } else {
              backup_service_->wakeup();
            }
          } 
        }
        if (OB_NOT_NULL(job_mgr)) {
          job_mgr->~ObIBackupJobMgr();
          ObBackupJobMgrAlloctor::free(job_mgr);
          job_mgr = nullptr;
        }
      }
    }
  }
  return ret;
}

/*
 *-----------------------------ObIBackupJobMgr-------------------------
 */

ObIBackupJobMgr::ObIBackupJobMgr()
 : is_inited_(false),
   tenant_id_(0),
   job_attr_(nullptr),
   sql_proxy_(nullptr),
   rpc_proxy_(nullptr),
   task_scheduler_(nullptr),
   schema_service_(nullptr),
   lease_service_(nullptr),
   backup_service_(nullptr)
{
}

int ObIBackupJobMgr::init(
    const uint64_t tenant_id, 
    ObBackupJobAttr &job_attr,
    common::ObMySQLProxy &sql_proxy,
    obrpc::ObSrvRpcProxy &rpc_proxy,
    ObBackupTaskScheduler &task_scheduler,
    ObBackupLeaseService  &lease_service,
    share::schema::ObMultiVersionSchemaService &schema_service,
    ObBackupService &backup_service)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("[DATA_BACKUP]init twice", K(ret));
  } else if (!job_attr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(job_attr));
  } else {
    tenant_id_ = tenant_id;
    job_attr_ = &job_attr;
    sql_proxy_ = &sql_proxy;
    rpc_proxy_ = &rpc_proxy;
    task_scheduler_ = &task_scheduler;
    schema_service_ = &schema_service;
    lease_service_ = &lease_service;
    backup_service_ = &backup_service;
    is_inited_ = true;
  }
  return ret;
}

void ObIBackupJobMgr::reset()
{
  tenant_id_ = OB_INVALID_TENANT_ID;
  job_attr_ = nullptr;
  sql_proxy_ = nullptr;
  rpc_proxy_ = nullptr;
  task_scheduler_ = nullptr;
  schema_service_ = nullptr;
  lease_service_ = nullptr;
  backup_service_ = nullptr;
  is_inited_ = false;
}

bool ObIBackupJobMgr::is_can_retry(const int err) const 
{
  return job_attr_->can_retry_ && ObBackupUtils::is_need_retry_error(err);
}

/*
 *-----------------------------ObUserTenantBackupJobMgr-------------------------
 */


int ObUserTenantBackupJobMgr::deal_non_reentrant_job(const int err) 
{
  int ret = OB_SUCCESS;
  ObBackupStatus next_status;
  next_status.status_ = ObBackupStatus::Status::FAILED;
  job_attr_->result_ = err;
  job_attr_->end_ts_ = ObTimeUtility::current_time();
  bool is_exist_set_task = true;
  bool is_start = false;
  LOG_INFO("start to deal non reentrant job", K(*job_attr_), KP(err));
  if (IS_NOT_INIT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]not init", K(ret));
  } else if (OB_FAIL(task_scheduler_->cancel_tasks(BackupJobType::BACKUP_DATA_JOB, job_attr_->job_id_, job_attr_->tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to cancel backup tasks", K(ret), K(*job_attr_));
  } else if (ObBackupStatus::Status::INIT == job_attr_->status_.status_) {
    if (OB_FAIL(advance_job_status(*sql_proxy_, next_status, err, job_attr_->end_ts_))) {
      LOG_WARN("[DATA_BACKUP]failed to move job status to FAILED", K(ret), KPC(job_attr_));
    } 
  } else if (ObBackupStatus::Status::DOING == job_attr_->status_.status_) {
    ObMySQLTransaction trans;
    ObBackupSetTaskMgr set_task_mgr;
    if (OB_FAIL(set_task_mgr.init(tenant_id_, *job_attr_, *sql_proxy_, 
        *rpc_proxy_, *task_scheduler_, *lease_service_, *schema_service_, *backup_service_))) {
      LOG_WARN("[DATA_BACKUP]failed to init set task mgr", K(ret));
    } else if (set_task_mgr.can_write_extern_infos(err) && OB_FAIL(set_task_mgr.write_backup_set_placeholder(is_start))) {
      LOG_WARN("fail to write extern infos", K(ret));
    } else if (OB_FAIL(trans.start(sql_proxy_, tenant_id_))) {
      LOG_WARN("fail to start trans", K(ret), K(tenant_id_));
    } else {
      if (OB_FAIL(set_task_mgr.deal_failed_set_task(trans))) {
        LOG_WARN("[DATA_BACKUP]failed to deal failed set task", K(ret));
      } else if (OB_FAIL(advance_job_status(trans, next_status, err, job_attr_->end_ts_))) {
        LOG_WARN("[DATA_BACKUP]failed to move job status to FAILED", K(ret), K(*job_attr_));
      } 
      if (OB_SUCC(ret)) {
        if (OB_FAIL(trans.end(true))) {
          LOG_WARN("fail to commit trans", K(ret));
        } 
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
          LOG_WARN("fail to roll back status", K(ret));
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    share::ObTaskId trace_id(*ObCurTraceId::get_trace_id());
    LOG_INFO("succeed deal with failed job, advance job status to FAILED", KPC(job_attr_));
    ROOTSERVICE_EVENT_ADD("backup_data", "deal with failed job", "tenant_id", 
      job_attr_->tenant_id_, "job_id", job_attr_->job_id_, "result", err, "trace_id", trace_id);
    backup_service_->wakeup();
  }
  return ret;
}

int ObUserTenantBackupJobMgr::process()
{
  //execute different operations according to the status of the job_attr
  //status including : INIT DOING COMPLETED FAILED CANCELING CANCELED
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[DATA_BACKUP]tenant backup data scheduler not init", K(ret));
  } else {
    ObBackupStatus::Status status = job_attr_->status_.status_;
    switch (status) {
      case ObBackupStatus::Status::INIT: {
        if (OB_FAIL(check_dest_validity_())) {
          LOG_WARN("[DATA_BACKUP]fail to check dest validity", K(ret));
        } else if (OB_FAIL(persist_set_task_())) {
          LOG_WARN("[DATA_BACKUP]failed to persist log stream task", K(ret), K(*job_attr_));
        }
        break;
      }
      case ObBackupStatus::Status::DOING: {
        if (OB_FAIL(do_set_task_())) {
          LOG_WARN("[DATA_BACKUP]failed to backup data", K(ret), K(*job_attr_));
        }
        break;
      }
      case ObBackupStatus::Status::COMPLETED:
      case ObBackupStatus::Status::FAILED:
      case ObBackupStatus::Status::CANCELED: {
        if (OB_FAIL(move_to_history_())) {
          LOG_WARN("[DATA_BACKUP]failed to move job and set to histroy", K(ret), K(*job_attr_));
        }
        break;
      }
      case ObBackupStatus::Status::CANCELING: {
        if (OB_FAIL(cancel_())) {
          LOG_WARN("[DATA_BACKUP]failed to cancel backup", K(ret), K(*job_attr_));
        }
        break;
      }
      default: {
        ret = OB_ERR_SYS;
        LOG_ERROR("[DATA_BACKUP]unknown backup status", K(ret), K(*job_attr_));
      }
    }
  }
  return ret;
}

int ObUserTenantBackupJobMgr::check_dest_validity_()
{
  int ret = OB_SUCCESS;
  ObBackupDestMgr dest_mgr;
  ObBackupPathString backup_dest_str;
  share::ObBackupDest backup_dest;
  // backup_path has no access id and key, get them from __all_backup_storage_info.
  if (OB_FAIL(ObBackupStorageInfoOperator::get_backup_dest(*sql_proxy_, job_attr_->tenant_id_, 
    job_attr_->backup_path_, backup_dest))) {
    LOG_WARN("fail to get backup dest", K(ret));
  } else if (OB_FAIL(backup_dest.get_backup_dest_str(backup_dest_str.ptr(), backup_dest_str.capacity()))) {
    LOG_WARN("fail to get backup dest str", K(ret));
  } else if (OB_FAIL(dest_mgr.init(job_attr_->tenant_id_, ObBackupDestType::TYPE::DEST_TYPE_BACKUP_DATA, 
    backup_dest_str, *sql_proxy_))) {
    LOG_WARN("fail to init dest manager", K(ret), KPC(job_attr_));
  } else if (OB_FAIL(dest_mgr.check_dest_validity(*rpc_proxy_, true/*need_format_file*/))) {
    LOG_WARN("fail to check backup dest validity", K(ret), KPC(job_attr_));
  }

  return ret;
}

int ObUserTenantBackupJobMgr::move_to_history_()
{
  int ret = OB_SUCCESS;
  LOG_INFO("start to move backup job to history", KPC(job_attr_));
  ObMySQLTransaction trans;
  ObBackupSetTaskMgr set_task_mgr;
  if (is_sys_tenant(job_attr_->initiator_tenant_id_) && OB_FAIL(report_failed_to_initiator_())) {
    LOG_WARN("fail to report job finish to initiator tenant id", K(ret), K(*job_attr_));
  } else if (OB_FAIL(trans.start(sql_proxy_, tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to start trans", K(ret));
  } else {
    if (OB_FAIL(set_task_mgr.init(tenant_id_, *job_attr_, *sql_proxy_, 
        *rpc_proxy_, *task_scheduler_, *lease_service_, *schema_service_, *backup_service_))) {
      if (OB_ENTRY_NOT_EXIST == ret) { // when job was canceled in INIT STATUS, there are no set task in task table.
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("[DATA_BACKUP]failed to init set task mgr", K(ret), K(*job_attr_));
      }
    } else if (OB_FAIL(set_task_mgr.do_clean_up(trans))) {
      LOG_WARN("[DATA_BACKUP]failed to do clean up", K(ret), K(set_task_mgr));
    } 
    
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObBackupJobOperator::move_job_to_his(trans, job_attr_->tenant_id_, job_attr_->job_id_))) {
      LOG_WARN("[DATA_BACKUP]failed to move job to history", K(ret), K(*job_attr_));
    } 

    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.end(true))) {
        LOG_WARN("[DATA_BACKUP]failed to commit trans", K(ret));
      } else {
        LOG_INFO("succeed to move job to history. backup job finish", "tenant_id", job_attr_->tenant_id_, "job_id", job_attr_->job_id_);
        backup_service_->wakeup();
      }
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
        LOG_WARN("[DATA_BACKUP]failed to roll back status", K(ret));
      }
    }
  }
  return ret;
}

int ObUserTenantBackupJobMgr::report_failed_to_initiator_()
{
  int ret = OB_SUCCESS;
  int result = OB_SUCCESS;
  ObSqlString sql;
  share::ObDMLSqlSplicer dml;
  int64_t affected_rows = -1;
  if (OB_SUCCESS == job_attr_->result_) {
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_TENANT_ID, OB_SYS_TENANT_ID))) {
    LOG_WARN("failed to add pk column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_JOB_ID, job_attr_->initiator_job_id_))) {
    LOG_WARN("failed to add pk column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_RESULT, job_attr_->result_))) {
    LOG_WARN("failed to add pk column", K(ret));
  } else if (OB_FAIL(dml.splice_update_sql(OB_ALL_BACKUP_JOB_TNAME, sql))) {
    LOG_WARN("failed to splice_update_sql", K(ret));
  } else if (OB_FAIL(sql.append_fmt(" and %s=%d", OB_STR_RESULT, result))) {
    LOG_WARN("failed to append sql", K(ret), K(sql));
  } else if (OB_FAIL(sql_proxy_->write(OB_SYS_TENANT_ID, sql.ptr(), affected_rows))) {
    LOG_WARN("failed to exec sql", K(ret), K(sql));
  } else if (1 < affected_rows) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid affected_rows, more than one job be updated is invalid", K(ret), K(affected_rows), K(sql));
  } else {
    FLOG_INFO("succeed to report report_backup_result_to_initiator");
  }
  return ret;
}

int ObUserTenantBackupJobMgr::check_can_backup_()
{
  int ret = OB_SUCCESS;
  ObTenantArchiveRoundAttr round_attr;
  if (OB_FAIL(ObTenantArchiveMgr::get_tenant_current_round(job_attr_->tenant_id_, job_attr_->incarnation_id_, round_attr))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_LOG_ARCHIVE_NOT_RUNNING;
      LOG_WARN("[DATA_BACKUP]not supported backup when log archive is not doing", K(ret), K(round_attr));
    } else {
      LOG_WARN("failed to get cur log archive round", K(ret), K(round_attr));
    }
  } else if (ObArchiveRoundState::Status::DOING != round_attr.state_.status_) {
    if (ObArchiveRoundState::Status::INTERRUPTED == round_attr.state_.status_) {
      ret = OB_LOG_ARCHIVE_INTERRUPTED;
    } else {
      ret = OB_LOG_ARCHIVE_NOT_RUNNING;
    }
    LOG_WARN("[DATA_BACKUP]not supported backup when log archive is not doing", K(ret), K(round_attr));
  }
  return ret;
}

int ObUserTenantBackupJobMgr::cancel_()
{
  int ret = OB_SUCCESS;
  ObBackupSetTaskMgr set_task_mgr;
  if (OB_FAIL(check_can_backup_())) {
    LOG_WARN("[DATA_BACKUP]failed to check can backup", K(ret), K(*job_attr_));
  } else if (OB_FAIL(update_set_task_to_canceling_())) {
    LOG_WARN("[DATA_BACKUP]failed to update set task to canceling", K(ret));
  } else {
    bool is_set_task_exist = true;
    if (OB_FAIL(set_task_mgr.init(tenant_id_, *job_attr_, *sql_proxy_, 
        *rpc_proxy_, *task_scheduler_, *lease_service_, *schema_service_, *backup_service_))) {
      if (OB_ENTRY_NOT_EXIST == ret) { // when job was canceled in INIT STATUS, there are no set task in task table.
        is_set_task_exist = false;
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("[DATA_BACKUP]failed to init set task mgr", K(ret), K(*job_attr_));
      }
    } else if (OB_FAIL(set_task_mgr.process())) {
      LOG_WARN("[DATA_BACKUP]failed to persist ls task", K(ret), K(set_task_mgr));
    } 
    
    if (OB_FAIL(ret)) {
    } else if (!is_set_task_exist || ObBackupStatus::Status::CANCELED == set_task_mgr.get_status()) {
      ObBackupStatus next_status(ObBackupStatus::Status::CANCELED);
      job_attr_->end_ts_ = ObTimeUtility::current_time();
      if (OB_FAIL(advance_job_status(*sql_proxy_, next_status, OB_CANCELED, job_attr_->end_ts_))) {
        LOG_WARN("[DATA_BACKUP]failed to advance_job_status", K(ret));
      } else {
        LOG_INFO("succeed to cancel job, advance job status to CANCELED", K(*job_attr_));
      }
    }
  }
  return ret;
}

int ObUserTenantBackupJobMgr::update_set_task_to_canceling_()
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = -1;
  ObDMLSqlSplicer dml;
  ObBackupStatus next_status;
  next_status.status_ = ObBackupStatus::Status::CANCELING;
  const char *comment = OB_SUCCESS == job_attr_->result_ ? "" : common::ob_strerror(job_attr_->result_);
  if (OB_FAIL(dml.add_pk_column(OB_STR_TENANT_ID, job_attr_->tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_STATUS, next_status.get_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_RESULT, job_attr_->result_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_COMMENT, comment))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.get_extra_condition().assign_fmt("%s=%ld", OB_STR_JOB_ID, job_attr_->job_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to get_extra_condition", K(ret));
  } else if (OB_FAIL(dml.splice_update_sql(OB_ALL_BACKUP_TASK_TNAME, sql))) {
    LOG_WARN("[DATA_BACKUP]failed to splice_update_sql", K(ret));
  } else if (OB_FAIL(sql_proxy_->write(gen_meta_tenant_id(job_attr_->tenant_id_), sql.ptr(), affected_rows))) {
    LOG_WARN("[DATA_BACKUP]failed to exec sql", K(ret), K(sql));
  } else if (affected_rows > 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("[DATA_BACKUP]invalid affected_rows", K(ret), K(affected_rows), K(sql), K(next_status));
  } else {
    LOG_INFO("[DATA_BACKUP]advance set task status", K(*job_attr_), K(next_status), K(sql));
  }
  return ret;
}

int ObUserTenantBackupJobMgr::do_set_task_()
{
  int ret = OB_SUCCESS;
  LOG_INFO("job is in doing backup set task", KPC(job_attr_));
  if (OB_FAIL(check_can_backup_())) {
    LOG_WARN("[DATA_BACKUP]failed to check can backup", K(ret), K(*job_attr_));
  } else { 
    ObBackupStatus next_status;
    ObBackupSetTaskMgr set_task_mgr;
    if (OB_FAIL(set_task_mgr.init(tenant_id_, *job_attr_, *sql_proxy_, 
          *rpc_proxy_, *task_scheduler_, *lease_service_, *schema_service_, *backup_service_))) {
      LOG_WARN("[DATA_BACKUP]failed to init set task mgr", K(ret));
    } else if (OB_FAIL(set_task_mgr.process())) {
      LOG_WARN("[DATA_BACKUP]failed to backup ls task", K(ret), K(set_task_mgr));
    } else if (OB_FALSE_IT(next_status.status_ = set_task_mgr.get_status())){
    } else if (ObBackupStatus::Status::COMPLETED == next_status.status_
        || ObBackupStatus::Status::FAILED == next_status.status_) {
      job_attr_->end_ts_ = ObTimeUtility::current_time();
      if (OB_FAIL(advance_job_status(*sql_proxy_, next_status, job_attr_->result_, job_attr_->end_ts_))) {
        LOG_WARN("[DATA_BACKUP]failed to advance status", K(ret), K(next_status), K(*job_attr_));
      } else {
        LOG_INFO("[DATA_BACKUP]succeed doing backup set task, and advancing job status to next status", 
            K(next_status), "tenant_id", job_attr_->tenant_id_, "job_id", job_attr_->job_id_);
        backup_service_->wakeup();
      }
    }
  }
  return ret;
}

int ObUserTenantBackupJobMgr::persist_set_task_()
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  LOG_INFO("start to persist set task", KPC(job_attr_));
  if (OB_FAIL(check_can_backup_())) {
    LOG_WARN("[DATA_BACKUP]failed to check can backup", K(ret), K(*job_attr_));
  } else if (OB_FAIL(trans.start(sql_proxy_, tenant_id_))) {
      LOG_WARN("[DATA_BACKUP]failed to start trans", K(ret));
  } else {
    ObBackupStatus next_status(ObBackupStatus::Status::DOING);
    if (OB_FAIL(insert_backup_set_task_(trans))) {
      LOG_WARN("[DATA_BACKUP]failed to insert backup set task", K(ret), K(*job_attr_));
    } else if (OB_FAIL(insert_backup_set_file_(trans))) {
      LOG_WARN("[DATA_BACKUP]failed to insert backup set file", K(ret), K(*job_attr_));
    } else if (OB_FAIL(advance_job_status(trans, next_status))) {
      LOG_WARN("fail to advance job status to doing", K(ret), KPC(job_attr_));
    } 

#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = E(EventTable::EN_BACKUP_PERSIST_SET_TASK_FAILED) OB_SUCCESS;
    }
#endif

    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.end(true))) {
        LOG_WARN("[DATA_BACKUP]failed to commit trans", K(ret));
      } else {
        ROOTSERVICE_EVENT_ADD("backup_data", "persist_set_task", "tenant_id", 
            job_attr_->tenant_id_, "job_id", job_attr_->job_id_);
        LOG_INFO("succeed to do persist set backup task, job advance status to DOING", 
            "tenant_id", job_attr_->tenant_id_, "job_id", job_attr_->job_id_);
        backup_service_->wakeup();
      }
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
        LOG_WARN("[DATA_BACKUP]failed to roll back status", K(ret));
      }
    }
  }
  return ret;
}

int ObUserTenantBackupJobMgr::insert_backup_set_task_(common::ObISQLClient &sql_proxy)
{
  int ret = OB_SUCCESS;
  ObBackupSetTaskAttr backup_set_task;
  if (OB_FAIL(backup_set_task.passwd_.assign(job_attr_->passwd_))) {
    LOG_WARN("[DATA_BACKUP]failed to assign passwd", K(ret), K(*job_attr_));
  } else if (OB_FAIL(backup_set_task.backup_path_.assign(job_attr_->backup_path_))) {
    LOG_WARN("[DATA_BACKUP]failed to assign backup dest", K(ret), K(*job_attr_));
  } else if (OB_FAIL(get_next_task_id_(sql_proxy, backup_set_task.task_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to get next task id", K(ret));
  } else if (OB_FAIL(ObBackupDataScheduler::get_scn(sql_proxy, job_attr_->tenant_id_, backup_set_task.start_scn_))) {
    LOG_WARN("fail t get start scn", K(ret));
  } else {
    backup_set_task.tenant_id_ = job_attr_->tenant_id_;
    backup_set_task.job_id_ = job_attr_-> job_id_;
    backup_set_task.incarnation_id_ = job_attr_->incarnation_id_;
    backup_set_task.backup_set_id_ = job_attr_->backup_set_id_;
    backup_set_task.status_.status_ = job_attr_->status_.status_;
    backup_set_task.encryption_mode_ = job_attr_->encryption_mode_;
    backup_set_task.result_ = job_attr_->result_;
    backup_set_task.start_ts_ = job_attr_->start_ts_;
    backup_set_task.meta_turn_id_ = 1;
    backup_set_task.data_turn_id_ = 1;
    backup_set_task.end_scn_ = palf::SCN::min_scn();
    backup_set_task.user_ls_start_scn_ = palf::SCN::min_scn();
    if (OB_FAIL(lease_service_->check_lease())) {
      LOG_WARN("fail to check leader", K(ret));
    } else if (OB_FAIL(ObBackupTaskOperator::insert_backup_task(sql_proxy, backup_set_task))) {
      LOG_WARN("[DATA_BACKUP]failed to insert backup task", K(ret), K(backup_set_task));
    }
  }
  return ret;
}

int ObUserTenantBackupJobMgr::insert_backup_set_file_(common::ObISQLClient &sql_proxy) 
{
  int ret = OB_SUCCESS;
  bool is_exist = false;
  int64_t prev_full_backup_set_id = -1;
  int64_t prev_inc_backup_set_id = -1;
  ObBackupSetFileDesc backup_set_desc;
  int64_t dest_id = 0;
  ObBackupDest backup_dest;
  if (OB_FAIL(ObBackupSetFileOperator::get_prev_backup_set_id(sql_proxy, job_attr_->tenant_id_, job_attr_->backup_set_id_, 
      job_attr_->backup_type_, job_attr_->backup_path_, prev_full_backup_set_id, prev_inc_backup_set_id))) {
    LOG_WARN("[DATA_BACKUP]fail to get prev backup set id", K(ret), K(*job_attr_));
  } else if (OB_FAIL(ObBackupStorageInfoOperator::get_backup_dest(sql_proxy, job_attr_->tenant_id_, job_attr_->backup_path_, backup_dest))) {
    LOG_WARN("[DATA_BACKUP]fail to get backup dest", K(ret), K(*job_attr_)); 
  } else if (OB_FAIL(ObBackupStorageInfoOperator::get_dest_id(sql_proxy, job_attr_->tenant_id_, backup_dest, dest_id))) {
    LOG_WARN("[DATA_BACKUP]fail to get dest id", K(ret), K(*job_attr_)); 
  } else if (OB_FAIL(fill_backup_set_desc_(*job_attr_, prev_full_backup_set_id, prev_inc_backup_set_id, dest_id, backup_set_desc))) {
    LOG_WARN("[DATA_BACKUP]fail to fill backup set desc", K(ret), K(*job_attr_), K(prev_full_backup_set_id), K(prev_inc_backup_set_id));
  } else if (OB_FAIL(lease_service_->check_lease())) {
    LOG_WARN("fail to check leader", K(ret));
  } else if (OB_FAIL(ObBackupSetFileOperator::insert_backup_set_file(sql_proxy, backup_set_desc))) {
    LOG_WARN("[DATA_BACKUP]fail to insert backup set file", K(ret), K(backup_set_desc), K(*job_attr_));
  }
  return ret;
}

int ObUserTenantBackupJobMgr::fill_backup_set_desc_(
    const ObBackupJobAttr &job_attr, 
    const int64_t prev_full_backup_set_id,
    const int64_t prev_inc_backup_set_id,
    const int64_t dest_id,
    ObBackupSetFileDesc &backup_set_desc)
{
  int ret = OB_SUCCESS;
  if (!job_attr.is_valid() || prev_inc_backup_set_id < 0 || prev_full_backup_set_id < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(job_attr), K(prev_inc_backup_set_id), K(prev_full_backup_set_id));
  } else if (OB_FAIL(ObBackupUtils::convert_timestamp_to_date(job_attr.start_ts_/*us*/, backup_set_desc.date_))) {
    LOG_WARN("[DATA_BACKUP]failed to get time date", K(ret), K(job_attr));
  } else if (OB_FAIL(backup_set_desc.passwd_.assign(job_attr.passwd_))) {
    LOG_WARN("[DATA_BACKUP]failed to assign passwd", K(ret));
  } else if (OB_FAIL(backup_set_desc.backup_path_.assign(job_attr.backup_path_))) {
    LOG_WARN("[DATA_BACKUP]failed to assign backup dest", K(ret));
  } else {
    backup_set_desc.backup_set_id_ = job_attr.backup_set_id_;
    backup_set_desc.tenant_id_ = job_attr.tenant_id_;
    backup_set_desc.incarnation_ = job_attr.incarnation_id_;
    backup_set_desc.dest_id_ = dest_id;
    backup_set_desc.backup_type_.type_ = job_attr.backup_type_.type_;
    backup_set_desc.prev_full_backup_set_id_ = prev_full_backup_set_id;
    backup_set_desc.prev_inc_backup_set_id_ = prev_inc_backup_set_id;

    backup_set_desc.start_time_ = job_attr.start_ts_;
    backup_set_desc.end_time_ = job_attr.end_ts_;
    backup_set_desc.status_ = ObBackupSetFileDesc::BackupSetStatus::DOING;
    backup_set_desc.file_status_ = ObBackupFileStatus::BACKUP_FILE_COPYING;
    backup_set_desc.result_ = job_attr.result_;
    backup_set_desc.encryption_mode_ = job_attr.encryption_mode_;
    backup_set_desc.start_replay_scn_ = palf::SCN::min_scn();
    backup_set_desc.min_restore_scn_ = palf::SCN::min_scn();
    backup_set_desc.backup_compatible_ = ObBackupSetFileDesc::Compatible::COMPATIBLE_VERSION_1;
    backup_set_desc.tenant_compatible_ = ObClusterVersion::get_instance().get_cluster_version();
    backup_set_desc.plus_archivelog_ = job_attr.plus_archivelog_;
  }
  return ret;
}

int ObUserTenantBackupJobMgr::get_next_task_id_(common::ObISQLClient &sql_proxy, int64_t &task_id)
{
  int ret = OB_SUCCESS;
  task_id = -1;
  if (OB_FAIL(ObLSBackupInfoOperator::get_next_task_id(sql_proxy, tenant_id_, task_id))) {
    LOG_WARN("[DATA_BACKUP]failed to get next task id", K(ret));
  } else if (-1 == task_id) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[DATA_BACKUP]invalid task id", K(ret), K(task_id));
  }
  return ret;
}

int ObUserTenantBackupJobMgr::advance_job_status(
    common::ObISQLClient &trans,
    const ObBackupStatus &next_status, 
    const int result, 
    const int64_t end_ts)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(lease_service_->check_lease())) {
    LOG_WARN("fail to check leader", K(ret));
  } else if (OB_FAIL(ObBackupJobOperator::advance_job_status(trans, *job_attr_, next_status, result, end_ts))) {
    LOG_WARN("[DATA_BACKUP]failed to advance job status", K(ret), K(*job_attr_), K(next_status), K(result), K(end_ts));
  } 
  return ret;
}

/*
 *-----------------------ObSysTenantBackupJobMgr------------------------
 */

int ObSysTenantBackupJobMgr::process()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[DATA_BACKUP]tenant backup data scheduler not init", K(ret));
  } else {
    ObBackupStatus::Status status = job_attr_->status_.status_;
    switch (status) {
      case ObBackupStatus::Status::INIT: {
        if (OB_FAIL(handle_user_tenant_backupdatabase_())) {
          LOG_WARN("[DATA_BACKUP]fail to inser user tenant job");
        }
        break;
      } 
      case ObBackupStatus::Status::DOING: {
        if (OB_FAIL(statistic_user_tenant_job_())) {
          LOG_WARN("[DATA_BACKUP]failed to backup data", K(ret), K(*job_attr_));
        }
        break;
      }
      case ObBackupStatus::Status::COMPLETED: 
      case ObBackupStatus::Status::FAILED: 
      case ObBackupStatus::Status::CANCELED: {
        if (OB_FAIL(move_to_history_())) {
          LOG_WARN("[DATA_BACKUP]failed to move job to histroy", K(ret), K(*job_attr_));
        }
        break;
      }
      case ObBackupStatus::Status::CANCELING: {
        if (OB_FAIL(cancel_user_tenant_job_())) {
          LOG_WARN("[DATA_BACKUP]failed to cancel backup job", K(ret), K(*job_attr_));
        }
        break;
      }
      default: {
        ret = OB_ERR_SYS;
        LOG_ERROR("[DATA_BACKUP]unknown backup status", K(ret), K(*job_attr_));
      }
    }
  }
  return ret;
}

int ObSysTenantBackupJobMgr::handle_user_tenant_backupdatabase_()
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  int64_t cnt = 0;
  ObBackupJobAttr new_job_attr;
  for (int64_t i = 0; OB_SUCC(ret) && i < job_attr_->executor_tenant_id_.count(); ++i) {
    is_valid = false;
    cnt = 0;
    const uint64_t user_tenant_id = job_attr_->executor_tenant_id_.at(i);
    if (OB_FAIL(ObBackupDataScheduler::check_tenant_status(*schema_service_, user_tenant_id, is_valid))) {
      LOG_WARN("fail to check tenant status", K(ret), K(user_tenant_id));
    } else if (!is_valid) {
      LOG_WARN("tenant status not valid, this tenant shouldn't be backup", K(user_tenant_id), K(is_valid));
    } else if (OB_FAIL(ObBackupJobOperator::cnt_jobs(*sql_proxy_, user_tenant_id, job_attr_->tenant_id_, cnt))) {
      LOG_WARN("fail to cnt user backup job initiated by cur sys tenant job", K(ret), K(user_tenant_id));
    } else if (cnt != 0) {
      LOG_INFO("user tenant job has been inserted, just pass", K(user_tenant_id));
    } else if (OB_FAIL(do_handle_user_tenant_backupdatabase_(user_tenant_id))) {
      if (OB_BACKUP_CAN_NOT_START == ret || OB_BACKUP_IN_PROGRESS == ret) { 
        LOG_WARN("tenant can't start backup now just pass", K(ret)); 
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to do insert user tenant job", K(ret), K(user_tenant_id));
      }
    }
  }
  if (OB_SUCC(ret)) {
    ObBackupStatus next_status(ObBackupStatus::Status::DOING);
    if (OB_FAIL(advance_status_(*sql_proxy_, next_status))) {
      LOG_WARN("fail to advance sys job status", K(ret), K(*job_attr_), K(next_status));
    } else {
      backup_service_->wakeup();
      LOG_INFO("succeed handle user tenant backupdatabase, advance sys job to DOING", K(*job_attr_), K(next_status));
    }
  }
  return ret;
}

int ObSysTenantBackupJobMgr::do_handle_user_tenant_backupdatabase_(const uint64_t &tenant_id)
{

  // TODO: adjust usr srv rpc proxy when has tenant backup thread.

  int ret = OB_SUCCESS;
  common::ObAddr rs_addr;
  obrpc::ObBackupDatabaseArg backup_database_arg;
  ObAddr leader;
  if (!is_valid_tenant_id(tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id", K(ret), K(tenant_id));
  } else {
    backup_database_arg.tenant_id_ = tenant_id;
    backup_database_arg.initiator_tenant_id_ = job_attr_->tenant_id_;
    backup_database_arg.initiator_job_id_ = job_attr_->job_id_;
    backup_database_arg.is_incremental_ = job_attr_->backup_type_.type_ == ObBackupType::BackupType::INCREMENTAL_BACKUP;
    backup_database_arg.is_compl_log_ = job_attr_->plus_archivelog_;
    backup_database_arg.encryption_mode_ = job_attr_->encryption_mode_;
    if (OB_FAIL(backup_database_arg.backup_dest_.assign(job_attr_->backup_path_))) {
      LOG_WARN("fail to assign backup dest", K(ret));
    } else if (OB_FAIL(backup_database_arg.backup_description_.assign(job_attr_->description_))) {
      LOG_WARN("fail to assign backup description", K(ret));
    } else if (OB_FAIL(backup_database_arg.passwd_.assign(job_attr_->passwd_))) {
      LOG_WARN("fail to assign passwd", K(ret));
    } else if (OB_ISNULL(GCTX.rs_rpc_proxy_) || OB_ISNULL(GCTX.rs_mgr_)) { 
      ret = OB_ERR_SYS;
      LOG_WARN("rootserver rpc proxy or rs mgr must not be NULL", K(ret), K(GCTX));
    } else if (OB_FAIL(GCTX.rs_mgr_->get_master_root_server(rs_addr))) {
      LOG_WARN("failed to get rootservice address", K(ret));
    } else if (OB_FAIL(GCTX.rs_rpc_proxy_->to(rs_addr).backup_database(backup_database_arg))) {
      LOG_WARN("failed to post backup ls data res", K(ret), K(backup_database_arg));
    } else {
      LOG_INFO("succeed handle user backup tenant", K(backup_database_arg));
    }
  }
  return ret;
}

int ObSysTenantBackupJobMgr::statistic_user_tenant_job_()
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  int64_t finish_user_backup_job  = 0;
  ObBackupJobAttr tmp_job_attr;
  LOG_INFO("sys tenant start to statistic user tenant job", K(*job_attr_));
  for (int64_t i = 0; OB_SUCC(ret) && i < job_attr_->executor_tenant_id_.count(); ++i) {
    const uint64_t user_tenant_id = job_attr_->executor_tenant_id_.at(i);
    is_valid = false;
    if (OB_FAIL(ObBackupDataScheduler::check_tenant_status(*schema_service_, user_tenant_id, is_valid))) {
      LOG_WARN("fail to check tenant status", K(ret), K(user_tenant_id));
    } else if (!is_valid) {
      finish_user_backup_job++;
      // TODO delete cancel tasks func when use tenant level backup thread.
      if (OB_FAIL(task_scheduler_->cancel_tasks(BackupJobType::BACKUP_DATA_JOB, user_tenant_id))) {
        LOG_WARN("fail to cancel task of tenant which has been dropped", K(ret), K(user_tenant_id));
      }
    } else if (OB_FAIL(ObBackupJobOperator::get_job(*sql_proxy_, false/*no update*/, 
        user_tenant_id, job_attr_->job_id_, true/**/, tmp_job_attr))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        finish_user_backup_job++;
        ret = OB_SUCCESS;
        LOG_INFO("tenant backup job has finished", K(user_tenant_id), KPC(job_attr_));
      } else {
        LOG_WARN("fail to get backup job", K(ret), K(user_tenant_id), "initiator job id", job_attr_->job_id_);
      }
    }
  }
  if (OB_SUCC(ret) && finish_user_backup_job == job_attr_->executor_tenant_id_.count()) {
    ObBackupStatus next_status;
    // for double check. 
    if (OB_FAIL(ObBackupJobOperator::get_job(*sql_proxy_, false/*no update*/, 
        job_attr_->tenant_id_, job_attr_->job_id_, false/*not initiator*/, tmp_job_attr))) {
      LOG_WARN("fail to get sys backup job", K(ret), "job_id", job_attr_->job_id_);
    } else if (tmp_job_attr.result_ != OB_SUCCESS) {
      next_status.status_ = ObBackupStatus::Status::FAILED;
    } else {
      next_status.status_ = ObBackupStatus::Status::COMPLETED;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(advance_status_(*sql_proxy_, next_status, tmp_job_attr.result_, ObTimeUtility::current_time()))) {
        LOG_WARN("fail to advance sys job status", K(ret), K(*job_attr_), K(next_status));
      } else {
        LOG_INFO("[DATA_BACKUP]user job finished, sys job move to next status", K(next_status), K(*job_attr_));
      }
    }
  }
  return ret;
}

int ObSysTenantBackupJobMgr::move_to_history_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(lease_service_->check_lease())) {
    LOG_WARN("fail to check leader", K(ret));
  } else if (OB_FAIL(ObBackupJobOperator::move_job_to_his(*sql_proxy_, job_attr_->tenant_id_, job_attr_->job_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to move job to history table", K(ret), K(*job_attr_));
  } else {
    LOG_INFO("sys tenant backup job succeed move to history table", K(*job_attr_));
  }
  return ret;
}

int ObSysTenantBackupJobMgr::cancel_user_tenant_job_()
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  int64_t finish_canceled_job = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < job_attr_->executor_tenant_id_.count(); ++i) {
    const uint64_t user_tenant_id = job_attr_->executor_tenant_id_.at(i);
    is_valid = false;
    ObBackupJobAttr tmp_job_attr;
    if (OB_FAIL(ObBackupDataScheduler::check_tenant_status(*schema_service_, user_tenant_id, is_valid))) {
      LOG_WARN("fail to check tenant status", K(ret), K(user_tenant_id));
    } else if (!is_valid) {
      finish_canceled_job++;
      LOG_WARN("tenant status not valid, just pass the tenant", K(user_tenant_id), K(is_valid));
    } else if (OB_FAIL(ObBackupJobOperator::get_job(*sql_proxy_, false/*no update*/, 
        user_tenant_id, job_attr_->job_id_, true/**/, tmp_job_attr))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        finish_canceled_job++;
        ret = OB_SUCCESS;
        LOG_INFO("tenant backup job has finished", K(user_tenant_id), KPC(job_attr_));
      } else {
        LOG_WARN("fail to get backup job", K(ret), K(user_tenant_id), "initiator job id", job_attr_->job_id_);
      }
    } else if (tmp_job_attr.status_.status_ == ObBackupStatus::Status::INIT
        || tmp_job_attr.status_.status_ == ObBackupStatus::Status::DOING) {
      ObBackupStatus next_status(ObBackupStatus::Status::CANCELING);
      if (OB_FAIL(lease_service_->check_lease())) {
        LOG_WARN("fail to check leader", K(ret));
      } else if (OB_FAIL(ObBackupJobOperator::advance_job_status(*sql_proxy_, tmp_job_attr, next_status))) {
        LOG_WARN("fail to advance user job to CANCELING", K(ret), K(tmp_job_attr), K(next_status));
      } else {
        FLOG_INFO("succeed advance user tenant job to CANCELING", K(tmp_job_attr));
      }
    } 
  }

  if (OB_SUCC(ret) && finish_canceled_job == job_attr_->executor_tenant_id_.count()) {
    ObBackupStatus next_status(ObBackupStatus::Status::CANCELED);
    if (OB_SUCC(ret)) {
      if (OB_FAIL(advance_status_(*sql_proxy_, next_status, OB_CANCELED, ObTimeUtility::current_time()))) {
        LOG_WARN("fail to advance sys job status", K(ret), K(*job_attr_), K(next_status));
      } else {
        FLOG_INFO("[DATA_BACKUP]succeed schedule sys backup job", K(*job_attr_));
      }
    }
  }
  return ret;
}

int ObSysTenantBackupJobMgr::advance_status_(
    common::ObISQLClient &sql_proxy, 
    const ObBackupStatus &next_status, 
    const int result, 
    const int64_t end_ts)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(lease_service_->check_lease())) {
    LOG_WARN("fail to check leader", K(ret));
  } else if (OB_FAIL(ObBackupJobOperator::advance_job_status(sql_proxy, *job_attr_, next_status, result, end_ts))) {
    LOG_WARN("[DATA_BACKUP]failed to advance job status", K(ret), K(*job_attr_), K(next_status), K(result), K(end_ts));
  }
  return ret;
}

/*
 *----------------------ObBackupJobMgrAlloctor-------------------------
 */
int ObBackupJobMgrAlloctor::alloc(const uint64_t tenant_id, ObIBackupJobMgr *&job_mgr)
{
  int ret = OB_SUCCESS;
  if (is_meta_tenant(tenant_id)) {
    job_mgr = OB_NEW(ObUserTenantBackupJobMgr, "UserJobMgr");
  } else if (is_sys_tenant(tenant_id)) {
    job_mgr = OB_NEW(ObSysTenantBackupJobMgr, "SysJobMgr");
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id, only meta tenant and sys tenant can alloc backup job mgr", K(tenant_id));
  }
  return ret;
}

void ObBackupJobMgrAlloctor::free(ObIBackupJobMgr *job_mgr)
{
  uint64_t tenant_id = OB_INVALID_TENANT_ID;
  if (OB_ISNULL(job_mgr)) {
  } else if (OB_FALSE_IT(tenant_id = job_mgr->get_tenant_id())) {
  } else if (is_sys_tenant(tenant_id)) {
    OB_DELETE(ObIBackupJobMgr, "SysJobMgr", job_mgr);
  } else if (is_meta_tenant(tenant_id)) {
    OB_DELETE(ObIBackupJobMgr, "UserJobMgr", job_mgr);
  } 
  job_mgr = nullptr;
}


} // namespace oceanbase
} // namespace rootserver
