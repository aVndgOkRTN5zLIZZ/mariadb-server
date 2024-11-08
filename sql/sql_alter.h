/* Copyright (c) 2010, 2014, Oracle and/or its affiliates.
   Copyright (c) 2013, 2021, MariaDB Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#ifndef SQL_ALTER_TABLE_H
#define SQL_ALTER_TABLE_H

#include "sql_class.h"
#include "table_cache.h"
#include <utility>


class Alter_drop;
class Alter_column;
class Alter_rename_key;
class Alter_index_ignorability;
class Key;


/* Backup for the table we altering */
class FK_table_backup
{
public:
  TABLE_SHARE *share;
  FK_list foreign_keys;
  FK_list referenced_keys;

  FK_table_backup() : share(NULL) {}
  virtual ~FK_table_backup()
  {
    if (share)
      rollback();
  }
  bool init(TABLE_SHARE *);
  void commit()
  {
    share= NULL;
  }
  void rollback()
  {
    DBUG_ASSERT(share);
    share->foreign_keys= foreign_keys;
    share->referenced_keys= referenced_keys;
    share= NULL;
  }
};


/* Backup for the table we refering or which referes us */
class FK_ref_backup : public FK_table_backup
{
public:
  bool install_shadow;
  FK_ref_backup() : install_shadow(false) {}
  virtual ~FK_ref_backup()
  {
    commit();
  }
};


/* DROP FK does not fail for non-existent ref (but other commands do) */
struct FK_table_to_lock
{
  Table_name table;
  bool fail; // fail on non-existent foreign/referenced table
  FK_table_to_lock(Table_name _table) : table(_table), fail(false) {}
  bool operator< (const FK_table_to_lock &rhs) const
  {
    return table.cmp(rhs.table) < 0;
  }
};


/**
  Data describing the table being created by CREATE TABLE or
  altered by ALTER TABLE.
*/

class Alter_info
{
public:

  enum enum_enable_or_disable { LEAVE_AS_IS, ENABLE, DISABLE };

  bool vers_prohibited(THD *thd) const;

  /**
     The different values of the ALGORITHM clause.
     Describes which algorithm to use when altering the table.
  */
  enum enum_alter_table_algorithm
  {
/*
  Use thd->variables.alter_algorithm for alter method. If this is also
  default then use the fastest possible ALTER TABLE method
  (INSTANT, NOCOPY, INPLACE, COPY)
*/
    ALTER_TABLE_ALGORITHM_DEFAULT,

    // Copy if supported, error otherwise.
    ALTER_TABLE_ALGORITHM_COPY,

    // In-place if supported, error otherwise.
    ALTER_TABLE_ALGORITHM_INPLACE,

    // No Copy will refuse any operation which does rebuild.
    ALTER_TABLE_ALGORITHM_NOCOPY,

    // Instant should allow any operation that changes metadata only.
    ALTER_TABLE_ALGORITHM_INSTANT,

    // When there is no specification of algorithm during alter table.
    ALTER_TABLE_ALGORITHM_NONE
  };


  /**
     The different values of the LOCK clause.
     Describes the level of concurrency during ALTER TABLE.
  */
  enum enum_alter_table_lock
  {
    // Maximum supported level of concurency for the given operation.
    ALTER_TABLE_LOCK_DEFAULT,

    // Allow concurrent reads & writes. If not supported, give error.
    ALTER_TABLE_LOCK_NONE,

    // Allow concurrent reads only. If not supported, give error.
    ALTER_TABLE_LOCK_SHARED,

    // Block reads and writes.
    ALTER_TABLE_LOCK_EXCLUSIVE
  };

  Lex_ident_db db;
  Lex_ident_table table_name;

  // Columns and keys to be dropped.
  List<Alter_drop>              drop_list;
  // FIXME: these two to be removed in MDEV-21052
  List<Alter_drop>              tmp_drop_list;
  uint                          tmp_old_fkeys;
  // Columns for ALTER_CHANGE_COLUMN_DEFAULT.
  List<Alter_column>            alter_list;
  // List of keys, used by both CREATE and ALTER TABLE.
  List<Key>                     key_list;
  // List of keys to be renamed.
  List<Alter_rename_key>        alter_rename_key_list;
  // List of columns, used by both CREATE and ALTER TABLE.
  List<Create_field>            create_list;
  // Indexes whose ignorability needs to be changed.
  List<Alter_index_ignorability>  alter_index_ignorability_list;
  List<Virtual_column_info>     check_constraint_list;
  // Type of ALTER TABLE operation.
  alter_table_operations        flags;
  ulong                         partition_flags;
  // Enable or disable keys.
  enum_enable_or_disable        keys_onoff;
  // Used only in add_stat_drop_index()
  TABLE                         *original_table;
  // List of partitions.
  List<const char>              partition_names;
  // Number of partitions.
  uint                          num_parts;

  /* List of fields that we should delete statistics from */
  List<Field> drop_stat_fields;

  struct DROP_INDEX_STAT_PARAMS
  {
    KEY *key;
    bool ext_prefixes_only;
  };

  struct RENAME_COLUMN_STAT_PARAMS
  {
    Field *field;
    LEX_CSTRING *name;
    uint duplicate_counter;                       // For temporary names
  };
  struct RENAME_INDEX_STAT_PARAMS
  {
    const KEY *key;
    const LEX_CSTRING *name;
    uint duplicate_counter;                       // For temporary names
    uint usage_count;                             // How many rename entries
  };

  /* List of index that we should delete statistics from */
  List<DROP_INDEX_STAT_PARAMS> drop_stat_indexes;

  List<RENAME_COLUMN_STAT_PARAMS> rename_stat_fields;

  List<RENAME_INDEX_STAT_PARAMS> rename_stat_indexes;

  bool add_stat_drop_index(KEY *key, bool ext_prefixes_only,
                           MEM_ROOT *mem_root)
  {
    DROP_INDEX_STAT_PARAMS *param;
    if (!(param= (DROP_INDEX_STAT_PARAMS*)
          alloc_root(mem_root, sizeof(*param))))
      return true;
    param->key=  key;
    param->ext_prefixes_only= ext_prefixes_only;
    return drop_stat_indexes.push_back(param, mem_root);
  }

  bool add_stat_drop_index(THD *thd, const LEX_CSTRING *key_name);

  bool add_stat_rename_index(const KEY *key, const LEX_CSTRING *name,
                             MEM_ROOT *mem_root)
  {
    RENAME_INDEX_STAT_PARAMS *param;
    if (!(param= (RENAME_INDEX_STAT_PARAMS*)
          alloc_root(mem_root, sizeof(*param))))
      return true;
    param->key=  key;
    param->name= name;
    param->usage_count= 0;
    return rename_stat_indexes.push_back(param, mem_root);
  }

  bool add_stat_rename_field(Field *field, LEX_CSTRING *name,
                             MEM_ROOT *mem_root)
  {
    RENAME_COLUMN_STAT_PARAMS *param;
    if (!(param= (RENAME_COLUMN_STAT_PARAMS*)
          alloc_root(mem_root, sizeof(*param))))
      return true;
    param->field= field;
    param->name=  name;
    param->duplicate_counter= 0;
    return rename_stat_fields.push_back(param, mem_root);
  }

  bool collect_renamed_fields(THD *thd);

  /* Delete/update statistics in EITS tables */
  void apply_statistics_deletes_renames(THD *thd, TABLE *table);

private:
  // Type of ALTER TABLE algorithm.
  enum_alter_table_algorithm    requested_algorithm;

public:
  // Type of ALTER TABLE lock.
  enum_alter_table_lock         requested_lock;


  Alter_info() :
    tmp_old_fkeys(0),
    flags(0), partition_flags(0),
    keys_onoff(LEAVE_AS_IS),
    original_table(0),
    num_parts(0),
    requested_algorithm(ALTER_TABLE_ALGORITHM_NONE),
    requested_lock(ALTER_TABLE_LOCK_DEFAULT)
  {}

  void reset()
  {
    drop_list.empty();
    tmp_drop_list.empty();
    tmp_old_fkeys= 0;
    alter_list.empty();
    key_list.empty();
    alter_rename_key_list.empty();
    create_list.empty();
    alter_index_ignorability_list.empty();
    check_constraint_list.empty();
    drop_stat_fields.empty();
    drop_stat_indexes.empty();
    rename_stat_fields.empty();
    rename_stat_indexes.empty();
    flags= 0;
    partition_flags= 0;
    keys_onoff= LEAVE_AS_IS;
    num_parts= 0;
    partition_names.empty();
    requested_algorithm= ALTER_TABLE_ALGORITHM_NONE;
    requested_lock= ALTER_TABLE_LOCK_DEFAULT;
  }


  /**
    Construct a copy of this object to be used for mysql_alter_table
    and mysql_create_table.

    Historically, these two functions modify their Alter_info
    arguments. This behaviour breaks re-execution of prepared
    statements and stored procedures and is compensated by always
    supplying a copy of Alter_info to these functions.

    @param  rhs       Alter_info to make copy of
    @param  mem_root  Mem_root for new Alter_info

    @note You need to use check the error in THD for out
    of memory condition after calling this function.
  */
  Alter_info(const Alter_info &rhs, MEM_ROOT *mem_root);


  /**
     Parses the given string and sets requested_algorithm
     if the string value matches a supported value.
     Supported values: INPLACE, COPY, DEFAULT

     @param  str    String containing the supplied value
     @retval false  Supported value found, state updated
     @retval true   Not supported value, no changes made
  */
  bool set_requested_algorithm(const LEX_CSTRING *str);


  /**
     Parses the given string and sets requested_lock
     if the string value matches a supported value.
     Supported values: NONE, SHARED, EXCLUSIVE, DEFAULT

     @param  str    String containing the supplied value
     @retval false  Supported value found, state updated
     @retval true   Not supported value, no changes made
  */

  bool set_requested_lock(const LEX_CSTRING *str);

  /**
    Set the requested algorithm to the given algorithm value
    @param algo_value	algorithm to be set
   */
  void set_requested_algorithm(enum_alter_table_algorithm algo_value);

  /**
     Returns the algorithm value in the format "algorithm=value"
  */
  const char* algorithm_clause(THD *thd) const;

  /**
     Returns the lock value in the format "lock=value"
  */
  const char* lock() const;

  /**
     Check whether the given result can be supported
     with the specified user alter algorithm.

     @param  thd            Thread handle
     @param  ha_alter_info  Structure describing changes to be done
                            by ALTER TABLE and holding data during
                            in-place alter
     @retval false  Supported operation
     @retval true   Not supported value
  */
  bool supports_algorithm(THD *thd,
                          const Alter_inplace_info *ha_alter_info);

  /**
     Check whether the given result can be supported
     with the specified user lock type.

     @param  ha_alter_info  Structure describing changes to be done
                            by ALTER TABLE and holding data during
                            in-place alter
     @retval false  Supported lock type
     @retval true   Not supported value
  */
  bool supports_lock(THD *thd, bool, Alter_inplace_info *ha_alter_info);

  /**
    Return user requested algorithm. If user does not specify
    algorithm then return alter_algorithm variable value.
   */
  enum_alter_table_algorithm algorithm(const THD *thd) const;
  bool algorithm_is_nocopy(const THD *thd) const;
  bool algorithm_not_specified() const
  {
    return requested_algorithm == ALTER_TABLE_ALGORITHM_NONE;
  }
  uint check_vcol_field(Item_field *f) const;

  bool add_alter_list(THD *thd, LEX_CSTRING name, LEX_CSTRING new_name,
                      bool exists);

private:
  Alter_info &operator=(const Alter_info &rhs); // not implemented
  Alter_info(const Alter_info &rhs);            // not implemented
};


/** Runtime context for ALTER TABLE. */
class Alter_table_ctx
{
public:
  Alter_table_ctx();

  Alter_table_ctx(THD *thd, TABLE_LIST *table_list, uint tables_opened_arg,
                  const LEX_CSTRING *new_db_arg, const LEX_CSTRING *new_name_arg);
  /* Ctor for mysql_create_table_no_lock() */
  Alter_table_ctx(const Lex_ident_db *db, const Lex_ident_table *table_name)
  {
    this->db= *db;
    this->table_name= *table_name;
    this->new_db= *db;
    this->tmp_name= *table_name;
    this->new_name= *table_name;
    table= NULL;
  }

  /**
     @return true if the table is moved to another database or a new table
     created by ALTER_PARTITION_CONVERT_OUT, false otherwise.
  */
  bool is_database_changed() const
  { return (new_db.str != db.str); };

  /**
     @return true if the table is renamed or a new table created by
     ALTER_PARTITION_CONVERT_OUT, false otherwise.
  */
  bool is_table_renamed() const
  { return (is_database_changed() || new_name.str != table_name.str); };

  /**
     @return filename (including .frm) for the new table.
  */
  const char *get_new_filename() const
  {
    DBUG_ASSERT(!tmp_table);
    return new_filename;
  }

  /**
     @return path to the original table.
  */
  const char *get_path() const
  {
    DBUG_ASSERT(!tmp_table);
    return path;
  }

  /**
     @return path to the new table.
  */
  const char *get_new_path() const
  {
    DBUG_ASSERT(!tmp_table);
    return new_path;
  }

  /**
     @return path to the temporary table created during ALTER TABLE.
  */
  const char *get_tmp_path() const
  { return tmp_path; }

  static constexpr size_t max_tmp_path()
  {
    return sizeof(tmp_path) - 1;
  }

  const LEX_CSTRING get_tmp_cstring_path() const
  {
    LEX_CSTRING tmp= { tmp_path, strlen(tmp_path) };
    return tmp;
  };

  /**
    Mark ALTER TABLE as needing to produce foreign key error if
    it deletes a row from the table being changed.
  */
  void set_fk_error_if_delete_row(FOREIGN_KEY_INFO *fk)
  {
    fk_error_if_delete_row= true;
    fk_error_id= fk->foreign_id.str;
    fk_error_table= fk->foreign_table.str;
  }

  void report_implicit_default_value_error(THD *thd, const TABLE_SHARE *) const;
public:
  Create_field *implicit_default_value_error_field= nullptr;
  bool         error_if_not_empty= false;
  uint         tables_opened= 0;
  Lex_ident_db db;
  Lex_ident_table table_name;
  LEX_CSTRING  storage_engine_name;
  LEX_CSTRING  alias;
  Lex_ident_db new_db;
  Lex_ident_table new_name;
  LEX_CSTRING  new_alias; // TODO: why new_alias is needed?
  Lex_ident_table tmp_name;
  LEX_CSTRING  tmp_storage_engine_name;
  LEX_CUSTRING tmp_id, id;
  char         tmp_buff[80];
  uchar        id_buff[MY_UUID_SIZE];
  char         storage_engine_buff[NAME_LEN], tmp_storage_engine_buff[NAME_LEN];
  bool         storage_engine_partitioned;
  bool         tmp_storage_engine_name_partitioned;
  // TODO: remove key_info_buffer, key_count args from mysql_prepare_create_table(), etc
  KEY          *key_info;
  uint         keys;
  TABLE        *table;

  /**
    Indicates that if a row is deleted during copying of data from old version
    of table to the new version ER_FK_CANNOT_DELETE_PARENT error should be
    emitted.
  */
  bool fk_error_if_delete_row= false;
  /** Name of foreign key for the above error. */
  const char *fk_error_id= nullptr;
  /** Name of table for the above error. */
  const char *fk_error_table= nullptr;
  bool modified_primary_key= false;
  /** Indicates that we are altering temporary table */
  bool tmp_table= false;
  struct FK_rename_col
  {
    Table_name table;
    Table_name altered_table;
    Lex_cstring col_name;
    Lex_cstring new_name;
    // NB: "operator<" is required for std::set
    bool operator< (const FK_rename_col &rhs) const
    {
      int ref_cmp= table.cmp(rhs.table);
      if (ref_cmp < 0)
        return true;
      if (ref_cmp > 0)
        return false;
      return col_name.cmp(rhs.col_name) < 0;
    }
  };
  struct FK_add_new
  {
    Table_name ref;
    Foreign_key *fk;
    bool self_ref() const
    {
      return ref.name.is_empty();
    }
  };
  struct FK_drop_old
  {
    Table_name ref;
    const FK_info *fk;
  };
  struct FK_null_changes
  {
    const FK_info *fk;
    Table_name table;
    typedef std::pair<Lex_ident_column *, Lex_ident_column *> column_pair_t;
    /* Pair of columns: (col from this table, col from another table) */
    typedef mbd::vector<column_pair_t> column_list;
    column_list columns;
  };
  // NB: vector simplifies memory management, for the data sizes of ALTER TABLE input
  // it is acceptable.
  typedef mbd::vector<FK_null_changes> FK_nullchg_list;

  mbd::vector<FK_add_new> fk_added; /* can contain self-refs */
  /*
    These five contain only non-self-refs. They are used to update info in
    referenced tables.
  */
  // NB: multiple foreign keys can utilize same column (see fk_prepare_rename())
  mbd::set<FK_rename_col> fk_renamed_cols;
  mbd::set<FK_rename_col> rk_renamed_cols;
  mbd::vector<FK_drop_old> fk_dropped;
  mbd::vector<Table_name> fk_renamed_table;
  mbd::vector<Table_name> rk_renamed_table;
  FK_nullchg_list fk_null_changes;
  FK_nullchg_list rk_null_changes;
  /** FK list prepared by prepare_create_table() */
  FK_list            foreign_keys;
  /** RK list inherited from old table + self-refs from prepare_create_table() */
  FK_list            referenced_keys;
  MDL_request_list fk_mdl_reqs;
  mbd::map<Table_name, Share_acquire, Table_name_lt> fk_shares;

  bool fk_prepare_rename(THD *thd, TABLE *table, Create_field *def,
                         mbd::set<FK_table_to_lock> &fk_tables_to_lock);
  TABLE_SHARE *fk_get_share(const FK_null_changes &null_changes, bool &not_found);
  bool fk_handle_alter(THD *thd);
  void fk_release_locks(THD *thd);

  FK_table_backup fk_table_backup;
  // NB: share is owned and released by fk_shares
  mbd::map<TABLE_SHARE *, FK_ref_backup> fk_ref_backup;
  // NB: backup is added only if not exists
  FK_ref_backup* fk_add_backup(TABLE_SHARE *share);
  void fk_rollback();
  bool fk_install_frms();

private:
  char new_filename[FN_REFLEN + 1];
  CharBuffer<NAME_LEN> new_name_buff;
  char tmp_name_buff[NAME_LEN + 1];
  char path[FN_REFLEN + 1];
  char new_path[FN_REFLEN + 1];
  char tmp_path[FN_REFLEN + 1];

  Alter_table_ctx &operator=(const Alter_table_ctx &rhs); // not implemented
  Alter_table_ctx(const Alter_table_ctx &rhs);            // not implemented
};


/**
  Sql_cmd_common_alter_table represents the common properties of the ALTER TABLE
  statements.
  @todo move Alter_info and other ALTER generic structures from Lex here.
*/
class Sql_cmd_common_alter_table : public Sql_cmd
{
protected:
  /**
    Constructor.
  */
  Sql_cmd_common_alter_table() = default;

  virtual ~Sql_cmd_common_alter_table() = default;

  enum_sql_command sql_command_code() const override
  {
    return SQLCOM_ALTER_TABLE;
  }
};

/**
  Sql_cmd_alter_table represents the generic ALTER TABLE statement.
  @todo move Alter_info and other ALTER specific structures from Lex here.
*/
class Sql_cmd_alter_table : public Sql_cmd_common_alter_table,
                            public Storage_engine_name
{
public:
  /**
    Constructor, used to represent a ALTER TABLE statement.
  */
  Sql_cmd_alter_table() = default;

  ~Sql_cmd_alter_table() = default;

  Storage_engine_name *option_storage_engine_name() override { return this; }

  bool execute(THD *thd) override;
};


/**
  Sql_cmd_alter_sequence represents the ALTER SEQUENCE statement.
*/
class Sql_cmd_alter_sequence : public Sql_cmd,
                               public DDL_options
{
public:
  /**
    Constructor, used to represent a ALTER TABLE statement.
  */
  Sql_cmd_alter_sequence(const DDL_options &options)
   :DDL_options(options)
  {}

  ~Sql_cmd_alter_sequence() = default;

  enum_sql_command sql_command_code() const override
  {
    return SQLCOM_ALTER_SEQUENCE;
  }
  bool execute(THD *thd) override;
};


/**
  Sql_cmd_alter_table_tablespace represents ALTER TABLE
  IMPORT/DISCARD TABLESPACE statements.
*/
class Sql_cmd_discard_import_tablespace : public Sql_cmd_common_alter_table
{
public:
  enum enum_tablespace_op_type
  {
    DISCARD_TABLESPACE, IMPORT_TABLESPACE
  };

  Sql_cmd_discard_import_tablespace(enum_tablespace_op_type tablespace_op_arg)
    : m_tablespace_op(tablespace_op_arg)
  {}

  bool execute(THD *thd) override;

private:
  const enum_tablespace_op_type m_tablespace_op;
};

#endif
