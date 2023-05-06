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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_inner_table_schema.h"

#include "share/schema/ob_schema_macro_define.h"
#include "share/schema/ob_schema_service_sql_impl.h"
#include "share/schema/ob_table_schema.h"
#include "share/scn.h"

namespace oceanbase
{
using namespace share::schema;
using namespace common;
namespace share
{

int ObInnerTableSchema::cdb_ob_log_restore_source_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_OB_LOG_RESTORE_SOURCE_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_OB_LOG_RESTORE_SOURCE_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT TENANT_ID,     ID,     TYPE,     VALUE,     RECOVERY_UNTIL_SCN   FROM OCEANBASE.__ALL_VIRTUAL_LOG_RESTORE_SOURCE;   )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_ob_log_restore_source_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_LOG_RESTORE_SOURCE_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_LOG_RESTORE_SOURCE_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT TENANT_ID,     ID,     TYPE,     VALUE,     RECOVERY_UNTIL_SCN   FROM OCEANBASE.__ALL_VIRTUAL_LOG_RESTORE_SOURCE   WHERE TENANT_ID=EFFECTIVE_TENANT_ID();   )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::v_ob_timestamp_service_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_TIMESTAMP_SERVICE_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_TIMESTAMP_SERVICE_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       TENANT_ID,       TS_TYPE,       TS_VALUE,       SVR_IP,       SVR_PORT     FROM       oceanbase.__all_virtual_timestamp_service     WHERE       ROLE = 'LEADER' AND SERVICE_EPOCH =       (SELECT MAX(SERVICE_EPOCH) FROM       oceanbase.__all_virtual_timestamp_service) )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_ob_external_table_files_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_EXTERNAL_TABLE_FILES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_EXTERNAL_TABLE_FILES_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       B.TABLE_NAME AS TABLE_NAME,       C.DATABASE_NAME AS TABLE_SCHEMA,       'P0' AS PARTITION_NAME,       A.FILE_URL AS FILE_URL,       A.FILE_SIZE AS FILE_SIZE     FROM        OCEANBASE.__ALL_EXTERNAL_TABLE_FILE A        INNER JOIN OCEANBASE.__ALL_TABLE B ON A.TABLE_ID = B.TABLE_ID AND B.TENANT_ID = 0        INNER JOIN OCEANBASE.__ALL_DATABASE C ON B.DATABASE_ID = C.DATABASE_ID AND C.TENANT_ID = 0     WHERE B.TABLE_TYPE = 14 AND (A.DELETE_VERSION = 9223372036854775807 OR A.DELETE_VERSION < A.CREATE_VERSION) )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::all_ob_external_table_files_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_ALL_OB_EXTERNAL_TABLE_FILES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_ALL_OB_EXTERNAL_TABLE_FILES_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       B.TABLE_NAME AS TABLE_NAME,       C.DATABASE_NAME AS TABLE_SCHEMA,       'P0' AS PARTITION_NAME,       A.FILE_URL AS FILE_URL,       A.FILE_SIZE AS FILE_SIZE     FROM        OCEANBASE.__ALL_EXTERNAL_TABLE_FILE A        INNER JOIN OCEANBASE.__ALL_TABLE B ON A.TABLE_ID = B.TABLE_ID AND B.TENANT_ID = 0        INNER JOIN OCEANBASE.__ALL_DATABASE C ON B.DATABASE_ID = C.DATABASE_ID AND C.TENANT_ID = 0     WHERE  B.TABLE_TYPE = 14           AND 0 = sys_privilege_check('table_acc', EFFECTIVE_TENANT_ID(), C.DATABASE_NAME, B.TABLE_NAME)           AND (A.DELETE_VERSION = 9223372036854775807 OR A.DELETE_VERSION < A.CREATE_VERSION) )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::gv_ob_px_p2p_datahub_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_OB_PX_P2P_DATAHUB_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_OB_PX_P2P_DATAHUB_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(         SELECT           SVR_IP,           SVR_PORT,           CAST(TRACE_ID AS CHAR(64)) AS TRACE_ID,           CAST(DATAHUB_ID AS SIGNED) AS DATAHUB_ID,           CAST(MESSAGE_TYPE AS CHAR(256)) AS MESSAGE_TYPE,           CAST(TENANT_ID AS SIGNED) as TENANT_ID,           CAST(HOLD_SIZE AS SIGNED) as HOLD_SIZE,           CAST(TIMEOUT_TS AS DATETIME) as TIMEOUT_TS,           CAST(START_TIME AS DATETIME) as START_TIME         FROM oceanbase.__all_virtual_px_p2p_datahub  )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::v_ob_px_p2p_datahub_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_PX_P2P_DATAHUB_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_PX_P2P_DATAHUB_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(         SELECT * FROM OCEANBASE.GV$OB_PX_P2P_DATAHUB     WHERE SVR_IP=HOST_IP() AND SVR_PORT=RPC_PORT() )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::gv_sql_join_filter_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_SQL_JOIN_FILTER_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_SQL_JOIN_FILTER_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(         SELECT           SVR_IP,           SVR_PORT,           CAST(NULL AS SIGNED) AS QC_SESSION_ID,           CAST(NULL AS SIGNED) AS QC_INSTANCE_ID,           CAST(NULL AS SIGNED) AS SQL_PLAN_HASH_VALUE,           CAST(OTHERSTAT_5_VALUE AS SIGNED) as FILTER_ID,           CAST(NULL AS SIGNED) as BITS_SET,           CAST(OTHERSTAT_1_VALUE AS SIGNED) as FILTERED,           CAST(OTHERSTAT_3_VALUE AS SIGNED) as PROBED,           CAST(NULL AS SIGNED) as ACTIVE,           CAST(TENANT_ID AS SIGNED) as CON_ID,           CAST(TRACE_ID AS CHAR(64)) as TRACE_ID         FROM oceanbase.__all_virtual_sql_plan_monitor         WHERE plan_operation = 'PHY_JOIN_FILTER'  )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::v_sql_join_filter_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_SQL_JOIN_FILTER_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_SQL_JOIN_FILTER_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT * FROM OCEANBASE.GV$SQL_JOIN_FILTER     WHERE SVR_IP=HOST_IP() AND SVR_PORT=RPC_PORT() )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_ob_table_stat_stale_info_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_TABLE_STAT_STALE_INFO_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_TABLE_STAT_STALE_INFO_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__( WITH V AS (SELECT   NVL(T.TENANT_ID, 0) AS TENANT_ID,   NVL(T.TABLE_ID, VT.TABLE_ID) AS TABLE_ID,   NVL(T.TABLET_ID, VT.TABLET_ID) AS TABLET_ID,   NVL(T.INSERTS, 0) + NVL(VT.INSERT_ROW_COUNT, 0) - NVL(T.LAST_INSERTS, 0) AS INSERTS,   NVL(T.UPDATES, 0) + NVL(VT.UPDATE_ROW_COUNT, 0) - NVL(T.LAST_UPDATES, 0) AS UPDATES,   NVL(T.DELETES, 0) + NVL(VT.DELETE_ROW_COUNT, 0) - NVL(T.LAST_DELETES, 0) AS DELETES   FROM   OCEANBASE.__ALL_MONITOR_MODIFIED T   FULL JOIN   OCEANBASE.__ALL_VIRTUAL_DML_STATS VT   ON T.TABLE_ID = VT.TABLE_ID   AND T.TABLET_ID = VT.TABLET_ID   AND VT.TENANT_ID = EFFECTIVE_TENANT_ID() ) SELECT   CAST(TM.DATABASE_NAME AS CHAR(128)) AS DATABASE_NAME,   CAST(TM.TABLE_NAME AS CHAR(128)) AS TABLE_NAME,   CAST(TM.PART_NAME AS CHAR(128)) AS PARTITION_NAME,   CAST(TM.SUB_PART_NAME AS CHAR(128)) AS SUBPARTITION_NAME,   CAST(TS.ROW_CNT AS SIGNED) AS LAST_ANALYZED_ROWS,   TS.LAST_ANALYZED AS LAST_ANALYZED_TIME,   CAST(TM.INSERTS AS SIGNED) AS INSERTS,   CAST(TM.UPDATES AS SIGNED) AS UPDATES,   CAST(TM.DELETES AS SIGNED) AS DELETES,   CAST(NVL(CAST(UP.VALCHAR AS SIGNED), CAST(GP.SPARE4 AS SIGNED)) AS SIGNED) STALE_PERCENT,   CAST(CASE NVL((TM.INSERTS + TM.UPDATES + TM.DELETES) > TS.ROW_CNT * NVL(CAST(UP.VALCHAR AS SIGNED), CAST(GP.SPARE4 AS SIGNED)) / 100,                 (TM.INSERTS + TM.UPDATES + TM.DELETES) > 0)         WHEN 0 THEN 'NO'         WHEN 1 THEN 'YES'        END AS CHAR(3)) AS IS_STALE FROM (SELECT   T.TENANT_ID,   T.TABLE_ID,   CASE T.PART_LEVEL WHEN 0 THEN T.TABLE_ID WHEN 1 THEN P.PART_ID WHEN 2 THEN SP.SUB_PART_ID END AS PARTITION_ID,   DB.DATABASE_NAME,   T.TABLE_NAME,   P.PART_NAME,   SP.SUB_PART_NAME,   NVL(V.INSERTS, 0) AS INSERTS,   NVL(V.UPDATES, 0) AS UPDATES,   NVL(V.DELETES, 0) AS DELETES FROM OCEANBASE.__ALL_TABLE T JOIN OCEANBASE.__ALL_DATABASE DB   ON T.TENANT_ID = DB.TENANT_ID AND DB.DATABASE_ID = T.DATABASE_ID LEFT JOIN OCEANBASE.__ALL_PART P   ON T.TENANT_ID = P.TENANT_ID AND T.TABLE_ID = P.TABLE_ID LEFT JOIN OCEANBASE.__ALL_SUB_PART SP   ON T.TENANT_ID = SP.TENANT_ID AND T.TABLE_ID = SP.TABLE_ID AND P.PART_ID = SP.PART_ID LEFT JOIN V ON T.TENANT_ID = V.TENANT_ID AND T.TABLE_ID = V.TABLE_ID AND V.TABLET_ID = CASE T.PART_LEVEL WHEN 0 THEN T.TABLET_ID WHEN 1 THEN P.TABLET_ID WHEN 2 THEN SP.TABLET_ID END WHERE T.TABLE_TYPE IN (0, 3, 6) UNION ALL SELECT   MIN(T.TENANT_ID),   MIN(T.TABLE_ID),   -1 AS PARTITION_ID,   DB.DATABASE_NAME,   T.TABLE_NAME,   NULL AS PART_NAME,   NULL AS SUB_PART_NAME,   SUM(NVL(V.INSERTS, 0)) AS INSERTS,   SUM(NVL(V.UPDATES, 0)) AS UPDATES,   SUM(NVL(V.DELETES, 0)) AS DELETES FROM OCEANBASE.__ALL_TABLE T JOIN OCEANBASE.__ALL_DATABASE DB   ON T.TENANT_ID = DB.TENANT_ID AND DB.DATABASE_ID = T.DATABASE_ID JOIN OCEANBASE.__ALL_PART P   ON T.TENANT_ID = P.TENANT_ID AND T.TABLE_ID = P.TABLE_ID LEFT JOIN V ON T.TENANT_ID = V.TENANT_ID AND T.TABLE_ID = V.TABLE_ID AND V.TABLET_ID = P.TABLET_ID WHERE T.TABLE_TYPE IN (0, 3, 6) AND T.PART_LEVEL = 1 GROUP BY DB.DATABASE_NAME,          T.TABLE_NAME UNION ALL SELECT   MIN(T.TENANT_ID),   MIN(T.TABLE_ID),   MIN(P.PART_ID) AS PARTITION_ID,   DB.DATABASE_NAME,   T.TABLE_NAME,   P.PART_NAME,   NULL AS SUB_PART_NAME,   SUM(NVL(V.INSERTS, 0)) AS INSERTS,   SUM(NVL(V.UPDATES, 0)) AS UPDATES,   SUM(NVL(V.DELETES, 0)) AS DELETES FROM OCEANBASE.__ALL_TABLE T JOIN OCEANBASE.__ALL_DATABASE DB   ON T.TENANT_ID = DB.TENANT_ID AND DB.DATABASE_ID = T.DATABASE_ID JOIN OCEANBASE.__ALL_PART P   ON T.TENANT_ID = P.TENANT_ID AND T.TABLE_ID = P.TABLE_ID JOIN OCEANBASE.__ALL_SUB_PART SP   ON T.TENANT_ID = SP.TENANT_ID AND T.TABLE_ID = SP.TABLE_ID AND P.PART_ID = SP.PART_ID LEFT JOIN V ON T.TENANT_ID = V.TENANT_ID AND T.TABLE_ID = V.TABLE_ID AND V.TABLET_ID = SP.TABLET_ID WHERE T.TABLE_TYPE IN (0, 3, 6) AND T.PART_LEVEL = 2 GROUP BY DB.DATABASE_NAME,         T.TABLE_NAME,         P.PART_NAME UNION ALL SELECT   MIN(T.TENANT_ID),   MIN(T.TABLE_ID),   -1 AS PARTITION_ID,   DB.DATABASE_NAME,   T.TABLE_NAME,   NULL AS PART_NAME,   NULL AS SUB_PART_NAME,   SUM(NVL(V.INSERTS, 0)) AS INSERTS,   SUM(NVL(V.UPDATES, 0)) AS UPDATES,   SUM(NVL(V.DELETES, 0)) AS DELETES FROM OCEANBASE.__ALL_TABLE T JOIN OCEANBASE.__ALL_DATABASE DB   ON T.TENANT_ID = DB.TENANT_ID AND DB.DATABASE_ID = T.DATABASE_ID JOIN OCEANBASE.__ALL_PART P   ON T.TENANT_ID = P.TENANT_ID AND T.TABLE_ID = P.TABLE_ID JOIN OCEANBASE.__ALL_SUB_PART SP   ON T.TENANT_ID = SP.TENANT_ID AND T.TABLE_ID = SP.TABLE_ID AND P.PART_ID = SP.PART_ID LEFT JOIN V ON T.TENANT_ID = V.TENANT_ID AND T.TABLE_ID = V.TABLE_ID AND V.TABLET_ID = SP.TABLET_ID WHERE T.TABLE_TYPE IN (0, 3, 6) AND T.PART_LEVEL = 2 GROUP BY DB.DATABASE_NAME,         T.TABLE_NAME ) TM LEFT JOIN OCEANBASE.__ALL_TABLE_STAT TS   ON TM.TENANT_ID = TS.TENANT_ID AND TM.TABLE_ID = TS.TABLE_ID AND TM.PARTITION_ID = TS.PARTITION_ID LEFT JOIN OCEANBASE.__ALL_OPTSTAT_USER_PREFS UP   ON TM.TENANT_ID = UP.TENANT_ID AND TM.TABLE_ID = UP.TABLE_ID AND UP.PNAME = 'STALE_PERCENT' JOIN OCEANBASE.__ALL_OPTSTAT_GLOBAL_PREFS GP   ON GP.SNAME = 'STALE_PERCENT' )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_ob_external_table_files_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_OB_EXTERNAL_TABLE_FILES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_OB_EXTERNAL_TABLE_FILES_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       A.TENANT_ID AS TENANT_ID,       B.TABLE_NAME AS TABLE_NAME,       C.DATABASE_NAME AS TABLE_SCHEMA,       'P0' AS PARTITION_NAME,       A.FILE_URL AS FILE_URL,       A.FILE_SIZE AS FILE_SIZE     FROM        OCEANBASE.__ALL_VIRTUAL_EXTERNAL_TABLE_FILE A        INNER JOIN OCEANBASE.__ALL_VIRTUAL_TABLE B ON A.TABLE_ID = B.TABLE_ID AND A.TENANT_ID=B.TENANT_ID        INNER JOIN OCEANBASE.__ALL_VIRTUAL_DATABASE C ON B.DATABASE_ID = C.DATABASE_ID AND B.TENANT_ID=C.TENANT_ID     WHERE B.TABLE_TYPE = 14 AND (A.DELETE_VERSION = 9223372036854775807 OR A.DELETE_VERSION < A.CREATE_VERSION) )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}


} // end namespace share
} // end namespace oceanbase
