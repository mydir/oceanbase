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

#define USING_LOG_PREFIX RS_RESTORE

#include "ob_restore_util.h"
#include "lib/lock/ob_mutex.h"
#include "share/restore/ob_restore_uri_parser.h"
#include "share/schema/ob_schema_mgr.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/backup/ob_backup_struct.h"
#include "share/backup/ob_backup_path.h"
#include "rootserver/ob_rs_event_history_table_operator.h"
#include "storage/backup/ob_backup_restore_util.h"
#include "share/backup/ob_archive_store.h"
#include "share/backup/ob_backup_data_store.h"
#include "share/restore/ob_restore_persist_helper.h"//ObRestorePersistHelper ObRestoreProgressPersistInfo
#include "logservice/palf/palf_base_info.h"//PalfBaseInfo
#include "storage/backup/ob_backup_extern_info_mgr.h"//ObExternLSMetaMgr
#include "storage/ls/ob_ls_meta_package.h"//ls_meta

using namespace oceanbase::common;
using namespace oceanbase;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;

/*-------------- physical restore --------------------------*/
int ObRestoreUtil::fill_physical_restore_job(
    const int64_t job_id,
    const obrpc::ObPhysicalRestoreTenantArg &arg,
    ObPhysicalRestoreJob &job)
{
  int ret = OB_SUCCESS;

  if (job_id < 0 || !arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(job_id), K(arg));
  } else {
    job.reset();
    job.init_restore_key(OB_SYS_TENANT_ID, job_id); 
    job.set_status(PhysicalRestoreStatus::PHYSICAL_RESTORE_CREATE_TENANT);
    job.set_tenant_name(arg.tenant_name_);
    job.set_initiator_tenant_id(OB_SYS_TENANT_ID);
    if (OB_FAIL(job.set_description(arg.description_))) {
      LOG_WARN("fail to set description", K(ret));
    }

    // check restore option
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObPhysicalRestoreOptionParser::parse(arg.restore_option_, job))) {
        LOG_WARN("fail to parse restore_option", K(ret), K(arg), K(job_id));
      } else if (OB_FAIL(job.set_restore_option(arg.restore_option_))){
        LOG_WARN("failed to set restore option", KR(ret), K(arg));
      } else if (job.get_kms_encrypt()) {
        if (OB_FAIL(job.set_kms_info(arg.kms_info_))) {
          LOG_WARN("failed to fill kms info", KR(ret), K(arg));
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(fill_backup_info_(arg, job))) {
        LOG_WARN("failed to fill backup info", KR(ret), K(arg), K(job));
      }
    }

    if (FAILEDx(job.set_passwd_array(arg.passwd_array_))) {
      LOG_WARN("failed to copy passwd array", K(ret), K(arg));
    }

    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < arg.table_items_.count(); i++) {
        const obrpc::ObTableItem &item = arg.table_items_.at(i);
        if (OB_FAIL(job.get_white_list().add_table_item(item))) {
          LOG_WARN("fail to add table item", KR(ret), K(item));
        }
      }
    }
  }

  LOG_INFO("finish fill_physical_restore_job", K(job_id), K(arg), K(job));
  return ret;
}

int ObRestoreUtil::record_physical_restore_job(
    common::ObISQLClient &sql_client,
    const ObPhysicalRestoreJob &job)
{
  int ret = OB_SUCCESS;
  if (!job.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(job));
  } else {
    bool has_job = false;
    ObPhysicalRestoreTableOperator restore_op;
    if (OB_FAIL(check_has_physical_restore_job(sql_client,
                                               job.get_tenant_name(),
                                               has_job))) {
      LOG_WARN("fail to check if job exist", K(ret), K(job));
    } else if (has_job) {
      ret = OB_RESTORE_IN_PROGRESS;
      LOG_WARN("restore tenant job already exist", K(ret), K(job));
    } else if (OB_FAIL(restore_op.init(&sql_client, OB_SYS_TENANT_ID))) {
      LOG_WARN("fail init restore op", K(ret));
    } else if (OB_FAIL(restore_op.insert_job(job))) {
      LOG_WARN("fail insert job and partitions", K(ret), K(job));
    }
  }
  return ret;
}

int ObRestoreUtil::insert_user_tenant_restore_job(
             common::ObISQLClient &sql_client,
             const ObString &tenant_name,
             const int64_t user_tenant_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_user_tenant(user_tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("not user tenant", KR(ret), K(user_tenant_id));
  } else {
    ObPhysicalRestoreTableOperator restore_op;
    ObPhysicalRestoreJob initaitor_job_info;
    ObPhysicalRestoreJob job_info;
    if (OB_FAIL(restore_op.init(&sql_client, OB_SYS_TENANT_ID))) {
      LOG_WARN("failed to init restore op", KR(ret), K(user_tenant_id));
    } else if (OB_FAIL(restore_op.get_job_by_tenant_name(
            tenant_name, initaitor_job_info))) {
      LOG_WARN("failed to get job by tenant name", KR(ret), K(tenant_name));
    } else if (OB_FAIL(job_info.assign(initaitor_job_info))) {
      LOG_WARN("failed to assign job info", KR(ret), K(initaitor_job_info));
    } else {
      ObMySQLTransaction trans;
      //TODO get tenant job_id, use tenant
      const int64_t job_id = initaitor_job_info.get_job_id();
      job_info.init_restore_key(user_tenant_id, job_id);
      job_info.set_tenant_id(user_tenant_id);
      job_info.set_status(share::PHYSICAL_RESTORE_PRE);
      job_info.set_initiator_job_id(job_info.get_job_id());
      job_info.set_initiator_tenant_id(OB_SYS_TENANT_ID);
      ObPhysicalRestoreTableOperator user_restore_op;
      ObRestorePersistHelper restore_persist_op;
      ObRestoreProgressPersistInfo persist_info;
      persist_info.key_.tenant_id_ = user_tenant_id;
      persist_info.key_.job_id_ = job_info.get_job_id();
      persist_info.restore_scn_ = job_info.get_restore_scn();
      const uint64_t exec_tenant_id = gen_meta_tenant_id(user_tenant_id);
      if (OB_FAIL(trans.start(&sql_client, exec_tenant_id))) {
        LOG_WARN("failed to start trans", KR(ret), K(exec_tenant_id));
      } else if (OB_FAIL(user_restore_op.init(&trans, user_tenant_id))) {
        LOG_WARN("failed to init restore op", KR(ret), K(user_tenant_id));
      } else if (OB_FAIL(restore_persist_op.init(user_tenant_id))) {
        LOG_WARN("failed to init restore persist op", KR(ret), K(user_tenant_id));
      } else if (OB_FAIL(user_restore_op.insert_job(job_info))) {
        LOG_WARN("failed to insert job", KR(ret), K(job_info));
      } else if (OB_FAIL(restore_persist_op.insert_initial_restore_progress(trans, persist_info))) {
        LOG_WARN("failed to insert persist info", KR(ret), K(persist_info));
      }
      if (trans.is_started()) {
        int temp_ret = OB_SUCCESS;
        bool commit = OB_SUCC(ret);
        if (OB_SUCCESS != (temp_ret = trans.end(commit))) {
          ret = (OB_SUCC(ret)) ? temp_ret : ret;
          LOG_WARN("trans end failed", KR(ret), KR(temp_ret), K(commit));
        }
      }
    }
  }
  return ret;
}


int ObRestoreUtil::check_has_physical_restore_job(
    common::ObISQLClient &sql_client,
    const ObString &tenant_name,
    bool &has_job)
{
  int ret = OB_SUCCESS;
  ObArray<ObPhysicalRestoreJob> jobs;
  has_job = false;
  ObPhysicalRestoreTableOperator restore_op;
  if (OB_FAIL(restore_op.init(&sql_client, OB_SYS_TENANT_ID))) {
    LOG_WARN("fail init restore op", K(ret));
  } else if (OB_FAIL(restore_op.get_jobs(jobs))) {
    LOG_WARN("fail get jobs", K(ret));
  } else {
    int64_t len = common::OB_MAX_TENANT_NAME_LENGTH_STORE;
    FOREACH_CNT_X(job, jobs, !has_job) {
      if (0 == job->get_tenant_name().case_compare(tenant_name)) {
        //nocase compare
        has_job = true;
      }
    }
  }
  return ret;
}

int ObRestoreUtil::fill_backup_info_(
    const obrpc::ObPhysicalRestoreTenantArg &arg,
    share::ObPhysicalRestoreJob &job)
{
  int ret = OB_SUCCESS;
  const bool has_multi_url = arg.multi_uri_.length() > 0;
  LOG_INFO("start fill backup path", K(arg));
  if (has_multi_url) {
    if(OB_FAIL(fill_multi_backup_path(arg, job))) {
      LOG_WARN("failed to fill multi backup path", K(ret), K(arg));
    }
  } else {
    if (OB_FAIL(fill_compat_backup_path(arg, job))) {
      LOG_WARN("failed to fill compat backup path", K(ret), K(arg));
    }
  }
  FLOG_INFO("finish fill backup path", K(arg), K(job));
  return ret;
}

int ObRestoreUtil::fill_multi_backup_path(
    const obrpc::ObPhysicalRestoreTenantArg &arg,
    share::ObPhysicalRestoreJob &job)
{
  int ret = OB_SUCCESS;

  // TODO: use restore preview url

  return ret;
}

int ObRestoreUtil::get_encrypt_backup_dest_format_str(
    const ObArray<ObString> &original_dest_list,
    common::ObArenaAllocator &allocator,
    common::ObString &encrypt_dest_str)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  int64_t length = OB_MAX_BACKUP_DEST_LENGTH * original_dest_list.count();
  if (0 == original_dest_list.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", KR(ret), K(original_dest_list));
  } else if (OB_ISNULL(buf = reinterpret_cast<char *>(allocator.alloc(length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", KR(ret));
  } else {
    ObBackupDest dest;
    char encrypt_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
    int64_t pos = 0;
    for (int i = 0; OB_SUCC(ret) && i < original_dest_list.count(); i++) {
      const common::ObString &item = original_dest_list.at(i);
      if (OB_FAIL(dest.set(item))) {
        LOG_WARN("failed to push back", KR(ret), K(item));
      } else if (OB_FAIL(dest.get_backup_dest_str(encrypt_str, sizeof(encrypt_str)))) {
        LOG_WARN("failed to get backup dest str", KR(ret), K(item));
      } else if (OB_FAIL(databuff_printf(buf, length, pos, "%s%s", 0 == i ? "" : ",", encrypt_str))) {
        LOG_WARN("failed to append uri", KR(ret), K(encrypt_str), K(pos), K(buf)); 
      }
    }
    if (OB_FAIL(ret)) {
    } else if (strlen(buf) <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected format str", KR(ret), K(buf)); 
    } else {
      encrypt_dest_str.assign_ptr(buf, strlen(buf));
      LOG_DEBUG("get format encrypt backup dest str", KR(ret), K(encrypt_dest_str));
    }
  }

  return ret;
}

int ObRestoreUtil::fill_compat_backup_path(
    const obrpc::ObPhysicalRestoreTenantArg &arg,
    share::ObPhysicalRestoreJob &job)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  ObArray<ObString> tenant_path_array;
  ObArray<ObRestoreBackupSetBriefInfo> backup_set_list;
  ObArray<ObBackupPiecePath> backup_piece_list;
  ObArray<ObBackupPathString> log_path_list;
  ObString tenant_dest_list;
  int64_t last_backup_set_idx = -1;
  if (!arg.multi_uri_.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(arg));
  } else if (OB_FAIL(ObPhysicalRestoreUriParser::parse(arg.uri_, allocator, tenant_path_array))) {
    LOG_WARN("fail to parse uri", K(ret), K(arg));
  } else if (OB_FAIL(get_encrypt_backup_dest_format_str(tenant_path_array, allocator, tenant_dest_list))) {
    LOG_WARN("failed to convert uri", K(ret), K(arg), K(tenant_path_array)); 
  } else if (OB_FAIL(job.set_backup_dest(tenant_dest_list))) {
    LOG_WARN("failed to copy backup dest", K(ret), K(arg));
  } else if (OB_FAIL(fill_restore_scn_(arg, tenant_path_array, job))) {
    LOG_WARN("fail to fill restore scn", K(ret), K(arg), K(tenant_path_array));
  } else if (OB_FAIL(get_restore_source(tenant_path_array, arg.passwd_array_, job.get_restore_scn(), 
      backup_set_list, backup_piece_list, log_path_list))) {
    LOG_WARN("fail to get restore source", K(ret), K(tenant_path_array), K(arg));
  } else if (OB_FAIL(do_fill_backup_path_(backup_set_list, backup_piece_list, log_path_list, job))) {
    LOG_WARN("fail to do fill backup path", K(backup_set_list), K(backup_piece_list), K(log_path_list));
  } else if (OB_FALSE_IT(last_backup_set_idx = backup_set_list.count() - 1)) {
  } else if (last_backup_set_idx < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid idx", K(ret), K(last_backup_set_idx), K(backup_set_list));
  } else if (OB_FAIL(do_fill_backup_info_(backup_set_list.at(last_backup_set_idx).backup_set_path_, job))) {
    LOG_WARN("fail to do fill backup info");
  }

  return ret;
}

int ObRestoreUtil::fill_restore_scn_(const obrpc::ObPhysicalRestoreTenantArg &arg, 
    const ObIArray<ObString> &tenant_path_array, share::ObPhysicalRestoreJob &job)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid() || tenant_path_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg), K(tenant_path_array));
  } else if (arg.with_restore_scn_) {
    // restore scn which is specified by user
    job.set_restore_scn(arg.restore_scn_);
  } else if (!arg.with_restore_scn_) {
    int64_t round_id = 0;
    int64_t piece_id = 0;
    palf::SCN max_checkpoint_scn = palf::SCN::min_scn();
    // restore to max checkpoint scn of log
    ARRAY_FOREACH_X(tenant_path_array, i, cnt, OB_SUCC(ret)) {
      const ObString &tenant_path = tenant_path_array.at(i);
      ObArchiveStore store;
      ObBackupDest dest;
      ObBackupFormatDesc format_desc;
      palf::SCN cur_max_checkpoint_scn = palf::SCN::min_scn();
      if (OB_FAIL(dest.set(tenant_path))) {
        LOG_WARN("fail to set dest", K(ret), K(tenant_path));
      } else if (OB_FAIL(store.init(dest))) {
        LOG_WARN("failed to init archive store", K(ret), K(tenant_path));
      } else if (OB_FAIL(store.read_format_file(format_desc))) {
        LOG_WARN("failed to read format file", K(ret), K(tenant_path));
      } else if (ObBackupDestType::TYPE::DEST_TYPE_ARCHIVE_LOG != format_desc.dest_type_) {
        LOG_INFO("skip data dir", K(tenant_path), K(format_desc));
      } else if (OB_FAIL(store.get_max_checkpoint_scn(format_desc.dest_id_, round_id, piece_id, cur_max_checkpoint_scn))) {
        LOG_WARN("fail to get max checkpoint scn", K(ret), K(format_desc));
      } else {
        max_checkpoint_scn = std::max(max_checkpoint_scn, cur_max_checkpoint_scn);
      }
    }
    if (OB_SUCC(ret)) {
      if (palf::SCN::min_scn() == max_checkpoint_scn) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid max checkpoint scn, no archvie tenant path", K(ret), K(tenant_path_array));
      } else {
        job.set_restore_scn(max_checkpoint_scn);
      }
    }
  } 
  return ret;
}

int ObRestoreUtil::get_restore_source(
    const ObIArray<ObString>& tenant_path_array,
    const common::ObString &passwd_array,
    const palf::SCN &restore_scn,
    ObIArray<ObRestoreBackupSetBriefInfo> &backup_set_list,
    ObIArray<ObBackupPiecePath> &backup_piece_list,
    ObIArray<ObBackupPathString> &log_path_list)
{
  int ret = OB_SUCCESS;
  palf::SCN restore_start_log_scn = palf::SCN::min_scn();
  if (OB_FAIL(get_restore_backup_set_array_(tenant_path_array, passwd_array, restore_scn,
      restore_start_log_scn, backup_set_list))) {
    LOG_WARN("fail to get restore backup set array", K(ret), K(tenant_path_array), K(restore_scn));
  } else if (OB_FAIL(get_restore_log_piece_array_(tenant_path_array, restore_start_log_scn, restore_scn,
      backup_piece_list, log_path_list))) {
    LOG_WARN("fail to get restore log piece array", K(ret), K(tenant_path_array), K(restore_start_log_scn),
        K(restore_scn));
  } else if (backup_set_list.empty() || backup_piece_list.empty() || log_path_list.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("no backup set path or log piece can be used to restore", K(ret),
        K(tenant_path_array), K(backup_set_list), K(backup_piece_list), K(log_path_list), K(restore_start_log_scn),
        K(restore_scn));
  }
  return ret;
}

int ObRestoreUtil::get_restore_backup_set_array_(
    const ObIArray<ObString> &tenant_path_array,
    const common::ObString &passwd_array,
    const palf::SCN &restore_scn,
    palf::SCN &restore_start_log_scn,
    ObIArray<ObRestoreBackupSetBriefInfo> &backup_set_list)
{
  int ret = OB_SUCCESS;
  if (tenant_path_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invaldi argument", K(ret), K(tenant_path_array));
  } else {
    ARRAY_FOREACH_X(tenant_path_array, i, cnt, OB_SUCC(ret)) {
      const ObString &tenant_path = tenant_path_array.at(i);
      share::ObBackupDataStore store;
      share::ObBackupDest backup_dest;
      ObBackupFormatDesc format_desc;
      if (OB_FAIL(backup_dest.set(tenant_path.ptr()))) {
        LOG_WARN("fail to set backup dest", K(ret), K(tenant_path));
      } else if (OB_FAIL(store.init(backup_dest))) {
        LOG_WARN("failed to init backup store", K(ret), K(tenant_path));
      } else if (OB_FAIL(store.read_format_file(format_desc))) {
        LOG_WARN("failed to read format file", K(ret), K(store));
      } else if (ObBackupDestType::DEST_TYPE_BACKUP_DATA != format_desc.dest_type_) {
        LOG_INFO("skip log dir", K(tenant_path), K(format_desc));
      } else if (!backup_set_list.empty()) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("It is not support to restore from multiple tenant backup paths", K(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "It is not support to restore from multiple tenant backup paths.");
      } else if (OB_FAIL(store.get_backup_set_array(passwd_array, restore_scn, restore_start_log_scn, backup_set_list))) {
        LOG_WARN("fail to get backup set array", K(ret));
      }
    }
  }
  return ret;
}

int ObRestoreUtil::get_restore_backup_piece_list_(
    const ObBackupDest &dest,
    const ObArray<share::ObBackupPath> &piece_array,
    ObIArray<ObBackupPiecePath> &backup_piece_list)
{
  int ret = OB_SUCCESS; 
  if (!dest.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dest is invalid", K(ret), K(dest));
  } else {
    for (int64_t j = 0; OB_SUCC(ret) && j < piece_array.count(); ++j) {
      const share::ObBackupPath &piece_path = piece_array.at(j);
      ObBackupPiecePath backup_piece_path;
      ObBackupDest piece_dest;
      if (OB_FAIL(piece_dest.set(piece_path.get_ptr(), dest.get_storage_info()))) {
        LOG_WARN("fail to set piece dest", K(ret), K(piece_path), K(dest)); 
      } else if (OB_FAIL(piece_dest.get_backup_dest_str(backup_piece_path.ptr(), backup_piece_path.capacity()))) {
        LOG_WARN("fail to get piece dest str", K(ret), K(piece_dest));
      } else if (OB_FAIL(backup_piece_list.push_back(backup_piece_path))) {
        LOG_WARN("fail to push backup piece list", K(ret));
      }
    }
  }

  return ret;
}

int ObRestoreUtil::get_restore_log_path_list_(
    const ObBackupDest &dest,
    ObIArray<share::ObBackupPathString> &log_path_list)
{
  int ret = OB_SUCCESS;
  ObBackupPathString log_path;
  if (!dest.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dest is invalid", K(ret), K(dest));
  } else if (OB_FAIL(dest.get_backup_dest_str(log_path.ptr(), log_path.capacity()))) {
    LOG_WARN("fail to get backup dest str", K(ret), K(dest));
  } else if (OB_FAIL(log_path_list.push_back(log_path))) {
    LOG_WARN("fail to push backup log path", K(ret), K(log_path));
  }
  return ret;
}

int ObRestoreUtil::get_restore_log_piece_array_(
    const ObIArray<ObString> &tenant_path_array,
    const palf::SCN &restore_start_log_scn,
    const palf::SCN &restore_end_log_scn,
    ObIArray<ObBackupPiecePath> &backup_piece_list,
    ObIArray<share::ObBackupPathString> &log_path_list)
{
  int ret = OB_SUCCESS;
  ObArray<share::ObBackupPath> piece_array;
  if (tenant_path_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invaldi argument", K(ret), K(tenant_path_array));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tenant_path_array.count(); ++i) {
      piece_array.reset();
      const ObString &tenant_path = tenant_path_array.at(i);
      ObArchiveStore store;
      ObBackupDest dest;
      ObBackupFormatDesc format_desc;
      if (OB_FAIL(dest.set(tenant_path))) {
        LOG_WARN("fail to set dest", K(ret), K(tenant_path));
      } else if (OB_FAIL(store.init(dest))) {
        LOG_WARN("failed to init archive store", K(ret), K(tenant_path));
      } else if (OB_FAIL(store.read_format_file(format_desc))) {
        LOG_WARN("failed to read format file", K(ret), K(tenant_path));
      } else if (ObBackupDestType::TYPE::DEST_TYPE_ARCHIVE_LOG != format_desc.dest_type_) {
        LOG_INFO("skip data dir", K(tenant_path), K(format_desc));
      } else if (OB_FAIL(store.get_piece_paths_in_range(restore_start_log_scn, restore_end_log_scn, piece_array))) {
        LOG_WARN("fail to get restore pieces", K(ret), K(restore_start_log_scn), K(restore_end_log_scn));
      } else if (OB_FAIL(get_restore_log_path_list_(dest, log_path_list))) {
        LOG_WARN("fail to get restore log path list", K(ret), K(dest));
      } else if (OB_FAIL(get_restore_backup_piece_list_(dest, piece_array, backup_piece_list))){
        LOG_WARN("fail to get restore backup piece list", K(ret), K(dest), K(piece_array));
      }
    }
  }
  return ret;
}

int ObRestoreUtil::do_fill_backup_path_(
    const ObIArray<ObRestoreBackupSetBriefInfo> &backup_set_list,
    const ObIArray<ObBackupPiecePath> &backup_piece_list,
    const ObIArray<ObBackupPathString> &log_path_list,
    share::ObPhysicalRestoreJob &job)
{
  int ret = OB_SUCCESS;
  if (backup_set_list.empty() || backup_piece_list.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(backup_set_list), K(backup_piece_list));
  } else if (OB_FAIL(job.get_multi_restore_path_list().set(backup_set_list, backup_piece_list, log_path_list))) {
    LOG_WARN("failed to set mutli restore path list", KR(ret));
  }
  return ret;
}

int ObRestoreUtil::do_fill_backup_info_(
    const share::ObBackupSetPath & backup_set_path,
    share::ObPhysicalRestoreJob &job)
{
  int ret = OB_SUCCESS;
  share::ObBackupDataStore store;
  HEAP_VARS_2((ObExternBackupSetInfoDesc, backup_set_info),
    (ObExternTenantLocalityInfoDesc, locality_info)) {
    if (backup_set_path.is_empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(backup_set_path));
    } else if (OB_FAIL(store.init(backup_set_path.ptr()))) {
      LOG_WARN("fail to init mgr", K(ret));
    } else if (OB_FAIL(store.read_backup_set_info(backup_set_info))) {
      LOG_WARN("fail to read backup set info", K(ret));
    } else if (OB_FAIL(store.read_tenant_locality_info(locality_info))) {
      LOG_WARN("fail to read locality info", K(ret));
    } else if (!backup_set_info.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid backup set file", K(ret), K(backup_set_info));
    } else if (OB_FAIL(job.set_backup_tenant_name(locality_info.tenant_name_.ptr()))) {
      LOG_WARN("fail to set backup tenant name", K(ret), "tenant name", locality_info.tenant_name_);
    } else if (OB_FAIL(job.set_backup_cluster_name(locality_info.cluster_name_.ptr()))) {
      LOG_WARN("fail to set backup cluster name", K(ret), "cluster name", locality_info.cluster_name_);
    } else {
      job.set_source_cluster_version(backup_set_info.backup_set_file_.tenant_compatible_);
      job.set_compat_mode(locality_info.compat_mode_);
      job.set_backup_tenant_id(backup_set_info.backup_set_file_.tenant_id_);
    }
  }
  return ret;
}

int ObRestoreUtil::recycle_restore_job(const uint64_t tenant_id,
                               common::ObMySQLProxy &sql_proxy,
                               const ObPhysicalRestoreJob &job_info)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  const int64_t job_id = job_info.get_job_id();
  const uint64_t exec_tenant_id = gen_meta_tenant_id(tenant_id);
  if (OB_UNLIKELY(!is_user_tenant(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(exec_tenant_id));
  } else if (OB_FAIL(trans.start(&sql_proxy, exec_tenant_id))) {
    LOG_WARN("failed to start trans", KR(ret), K(exec_tenant_id));
  } else {
    ObPhysicalRestoreTableOperator restore_op;
    if (OB_FAIL(restore_op.init(&trans, tenant_id))) {
      LOG_WARN("failed to init restore op", KR(ret), K(tenant_id));
    } else if (OB_FAIL(restore_op.remove_job(job_id))) {
      LOG_WARN("failed to remove job", KR(ret), K(tenant_id), K(job_id));
    } else {
      ObHisRestoreJobPersistInfo history_info;
      ObRestoreProgressPersistInfo restore_progress;
      ObRestorePersistHelper persist_helper;
      ObRestoreJobPersistKey key;
      common::ObArray<share::ObLSRestoreProgressPersistInfo> ls_restore_progress_infos;
      key.tenant_id_ = tenant_id;
      key.job_id_ = job_info.get_job_id();
      if (OB_FAIL(persist_helper.init(tenant_id))) {
        LOG_WARN("failed to init persist helper", KR(ret), K(tenant_id));
      } else if (OB_FAIL(persist_helper.get_restore_process(
                     trans, key, restore_progress))) {
        LOG_WARN("failed to get restore progress", KR(ret), K(key));
      } else if (OB_FAIL(history_info.init_with_job_process(
                     job_info, restore_progress))) {
        LOG_WARN("failed to init history", KR(ret), K(job_info), K(restore_progress));
      } else if (OB_FAIL(persist_helper.get_all_ls_restore_progress(trans, ls_restore_progress_infos))) {
        LOG_WARN("failed to get ls restore progress", K(ret));
      } else {
        ARRAY_FOREACH_X(ls_restore_progress_infos, i, cnt, OB_SUCC(ret)) {
          const ObLSRestoreProgressPersistInfo &ls_restore_info = ls_restore_progress_infos.at(i);
          if (ls_restore_info.status_.is_restore_failed()) {
            char buf[OB_MAX_SERVER_ADDR_SIZE] = { 0 };
            if (OB_FAIL(ls_restore_info.key_.addr_.ip_port_to_string(buf, OB_MAX_SERVER_ADDR_SIZE))) {
              LOG_WARN("fail to addr to string", K(ret));
            } else if (OB_FAIL(history_info.comment_.append_fmt("ls_id: %ld, addr: %.*s, %.*s;", 
                ls_restore_info.key_.ls_id_.id(), static_cast<int>(OB_MAX_SERVER_ADDR_SIZE), buf, 
                static_cast<int>(ls_restore_info.comment_.length()), ls_restore_info.comment_.ptr()))) {
              LOG_WARN("fail to append fmt", K(ret));
            } 
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(persist_helper.insert_restore_job_history(
                     trans, history_info))) {
        LOG_WARN("failed to insert restore job history", KR(ret), K(history_info));
      }
    }
  }
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      ret = OB_SUCC(ret) ? tmp_ret : ret;
      LOG_WARN("failed to end trans", KR(ret), K(tmp_ret));
    }
  }
  return ret;
}

int ObRestoreUtil::recycle_restore_job(common::ObMySQLProxy &sql_proxy,
                          const share::ObPhysicalRestoreJob &job_info,
                          const ObHisRestoreJobPersistInfo &history_info)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  const int64_t job_id = job_info.get_job_id();
  const int64_t tenant_id = job_info.get_restore_key().tenant_id_;
  const uint64_t exec_tenant_id = gen_meta_tenant_id(tenant_id);
  ObRestorePersistHelper persist_helper;
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == exec_tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(exec_tenant_id));
  } else if (OB_FAIL(trans.start(&sql_proxy, exec_tenant_id))) {
    LOG_WARN("failed to start trans", KR(ret), K(exec_tenant_id));
  } else if (OB_FAIL(persist_helper.init(tenant_id))) {
    LOG_WARN("failed to init persist helper", KR(ret));
   } else if (OB_FAIL(persist_helper.insert_restore_job_history(trans, history_info))) {
    LOG_WARN("failed to insert restore job history", KR(ret), K(history_info));
  } else {
    ObPhysicalRestoreTableOperator restore_op;
    if (OB_FAIL(restore_op.init(&trans, tenant_id))) {
      LOG_WARN("failed to init restore op", KR(ret), K(tenant_id));
    } else if (OB_FAIL(restore_op.remove_job(job_id))) {
      LOG_WARN("failed to remove job", KR(ret), K(tenant_id), K(job_id));
    } else if (is_sys_tenant(tenant_id)) {
      //finish __all_rootservice_job
      int tmp_ret = PHYSICAL_RESTORE_SUCCESS == job_info.get_status() ? OB_SUCCESS : OB_ERROR;
      if (OB_FAIL(RS_JOB_COMPLETE(job_id, tmp_ret, trans))) {
        LOG_WARN("failed to complete job", KR(ret), K(job_id));
      }
    }
  }
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      ret = OB_SUCC(ret) ? tmp_ret : ret;
      LOG_WARN("failed to end trans", KR(ret), K(tmp_ret));
    }
  }
  return ret;
}
int ObRestoreUtil::get_user_restore_job_history(common::ObISQLClient &sql_client,
                                          const uint64_t user_tenant_id,
                                         const uint64_t initiator_tenant_id,
                                         const int64_t initiator_job_id,
                                         ObHisRestoreJobPersistInfo &history_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_user_tenant(user_tenant_id)
                  || OB_INVALID_TENANT_ID == initiator_tenant_id
                  || 0 > initiator_job_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(user_tenant_id),
    K(initiator_job_id), K(initiator_tenant_id));
  } else {
    ObRestorePersistHelper user_persist_helper;
    if (OB_FAIL(user_persist_helper.init(user_tenant_id))) {
      LOG_WARN("failed to init persist helper", KR(ret), K(user_tenant_id));
    } else if (OB_FAIL(user_persist_helper.get_restore_job_history(
                   sql_client, initiator_job_id, initiator_tenant_id,
                   history_info))) {
      LOG_WARN("failed to get restore progress", KR(ret), K(initiator_job_id), K(initiator_tenant_id));
    }
  }
  return ret;
}

int ObRestoreUtil::get_restore_ls_palf_base_info(
    const share::ObPhysicalRestoreJob &job_info, const ObLSID &ls_id,
    palf::PalfBaseInfo &palf_base_info)
{
  int ret = OB_SUCCESS;
  share::ObBackupDataStore store;
  const common::ObSArray<share::ObBackupSetPath> &backup_set_array = 
    job_info.get_multi_restore_path_list().get_backup_set_path_list();
  const int64_t idx = backup_set_array.count() - 1;
  storage::ObLSMetaPackage ls_meta_package;
  if (idx < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("backup_set_array can't empty", KR(ret), K(job_info));
  } else if (OB_FAIL(store.init(backup_set_array.at(idx).ptr()))) {
    LOG_WARN("fail to init backup data store", KR(ret));
  } else if (OB_FAIL(store.read_ls_meta_infos(ls_id, ls_meta_package))) {
    LOG_WARN("fail to read backup set info", KR(ret));
  } else if (!ls_meta_package.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid backup set info", KR(ret), K(ls_meta_package));
  } else {
    palf_base_info = ls_meta_package.palf_meta_;
    LOG_INFO("[RESTORE] get restore ls palf base info", K(palf_base_info));
  }
  return ret;
}
