// Copyright 2010-2014 RethinkDB, all rights reserved.
#include "clustering/administration/datum_adapter.hpp"

#include "clustering/administration/servers/config_client.hpp"
#include "clustering/administration/tables/database_metadata.hpp"
#include "rdb_protocol/pseudo_time.hpp"

ql::datum_t convert_string_to_datum(
        const std::string &value) {
    return ql::datum_t(datum_string_t(value));
}

bool convert_string_from_datum(
        const ql::datum_t &datum,
        std::string *value_out,
        std::string *error_out) {
    if (datum.get_type() != ql::datum_t::R_STR) {
        *error_out = "Expected a string; got " + datum.print();
        return false;
    }
    *value_out = datum.as_str().to_std();
    return true;
}

ql::datum_t convert_name_to_datum(
        const name_string_t &value) {
    return ql::datum_t(value.c_str());
}

bool convert_name_from_datum(
        ql::datum_t datum,
        const std::string &what,
        name_string_t *value_out,
        std::string *error_out) {
    if (datum.get_type() != ql::datum_t::R_STR) {
        *error_out = "Expected a " + what + "; got " + datum.print();
        return false;
    }
    if (!value_out->assign_value(datum.as_str())) {
        *error_out = datum.print() + " is not a valid " + what + "; " +
            std::string(name_string_t::valid_char_msg);
        return false;
    }
    return true;
}

ql::datum_t convert_uuid_to_datum(
        const uuid_u &value) {
    return ql::datum_t(datum_string_t(uuid_to_str(value)));
}

bool convert_uuid_from_datum(
        ql::datum_t datum,
        uuid_u *value_out,
        std::string *error_out) {
    if (datum.get_type() != ql::datum_t::R_STR) {
        *error_out = "Expected a UUID; got " + datum.print();
        return false;
    }
    if (!str_to_uuid(datum.as_str().to_std(), value_out)) {
        *error_out = "Expected a UUID; got " + datum.print();
        return false;
    }
    return true;
}

ql::datum_t convert_name_or_uuid_to_datum(
        const name_string_t &name,
        const uuid_u &uuid,
        admin_identifier_format_t identifier_format) {
    if (identifier_format == admin_identifier_format_t::name) {
        return convert_name_to_datum(name);
    } else {
        return convert_uuid_to_datum(uuid);
    }
}

bool convert_server_id_to_datum(
        const server_id_t &server_id,
        admin_identifier_format_t identifier_format,
        server_config_client_t *server_config_client,
        ql::datum_t *server_name_or_uuid_out,
        name_string_t *server_name_out) {
    boost::optional<name_string_t> name =
        server_config_client->get_name_for_server_id(server_id);
    if (!static_cast<bool>(name)) {
        return false;
    }
    if (server_name_or_uuid_out != nullptr) {
        *server_name_or_uuid_out =
            convert_name_or_uuid_to_datum(*name, server_id, identifier_format);
    }
    if (server_name_out != nullptr) *server_name_out = *name;
    return true;
}

bool convert_server_id_from_datum(
        const ql::datum_t &server_name_or_uuid,
        admin_identifier_format_t identifier_format,
        server_config_client_t *server_config_client,
        server_id_t *server_id_out,
        name_string_t *server_name_out,
        std::string *error_out) {
    if (identifier_format == admin_identifier_format_t::name) {
        name_string_t name;
        if (!convert_name_from_datum(server_name_or_uuid,
                "server name", &name, error_out)) {
            return false;
        }
        bool ok;
        server_config_client->get_name_to_server_id_map()->apply_read(
            [&](const std::multimap<name_string_t, server_id_t> *map) {
                if (map->count(name) == 0) {
                    *error_out = strprintf("Server `%s` does not exist.", name.c_str());
                    ok = false;
                } else if (map->count(name) > 1) {
                    *error_out = strprintf("Server `%s` is ambiguous; there are "
                        "multiple servers with that name.", name.c_str());
                    ok = false;
                } else {
                    if (server_id_out != nullptr) {
                        *server_id_out = map->find(name)->second;
                    }
                    if (server_name_out != nullptr) *server_name_out = name;
                    ok = true;
                }
            });
        return ok;
    } else {
        server_id_t id;
        if (!convert_uuid_from_datum(server_name_or_uuid, &id, error_out)) {
            return false;
        }
        boost::optional<name_string_t> name =
            server_config_client->get_name_for_server_id(id);
        if (!static_cast<bool>(name)) {
            *error_out = strprintf("There is no server with UUID `%s`.",
                uuid_to_str(id).c_str());
            return false;
        }
        if (server_id_out != nullptr) *server_id_out = id;
        if (server_name_out != nullptr) *server_name_out = *name;
        return true;
    }
}

bool convert_table_id_to_datums(
        const namespace_id_t &table_id,
        admin_identifier_format_t identifier_format,
        const cluster_semilattice_metadata_t &metadata,
        /* Any of these can be `nullptr` if they are not needed */
        ql::datum_t *table_name_or_uuid_out,
        name_string_t *table_name_out,
        ql::datum_t *db_name_or_uuid_out,
        name_string_t *db_name_out) {
    auto it = metadata.rdb_namespaces->namespaces.find(table_id);
    if (it == metadata.rdb_namespaces->namespaces.end() || it->second.is_deleted()) {
        return false;
    }
    name_string_t table_name = it->second.get_ref().name.get_ref();
    if (table_name_or_uuid_out != nullptr) {
        *table_name_or_uuid_out = convert_name_or_uuid_to_datum(
            table_name, table_id, identifier_format); 
    }
    if (table_name_out != nullptr) *table_name_out = table_name;
    database_id_t db_id = it->second.get_ref().database.get_ref();
    name_string_t db_name;
    auto jt = metadata.databases.databases.find(db_id);
    if (jt == metadata.databases.databases.end() || jt->second.is_deleted()) {
        db_name = name_string_t::guarantee_valid("__deleted_database__");
    } else {
        db_name = jt->second.get_ref().name.get_ref();
    }
    if (db_name_or_uuid_out != nullptr) {
        *db_name_or_uuid_out = convert_name_or_uuid_to_datum(
            db_name, db_id, identifier_format);
    }
    if (db_name_out != nullptr) *db_name_out = db_name;
    return true;
}

bool convert_database_id_to_datum(
        const database_id_t &db_id,
        admin_identifier_format_t identifier_format,
        const cluster_semilattice_metadata_t &metadata,
        ql::datum_t *db_name_or_uuid_out,
        name_string_t *db_name_out) {
    auto it = metadata.databases.databases.find(db_id);
    guarantee(it != metadata.databases.databases.end());
    if (it->second.is_deleted()) {
        return false;
    }
    name_string_t db_name = it->second.get_ref().name.get_ref();
    if (db_name_or_uuid_out != nullptr) {
        *db_name_or_uuid_out =
            convert_name_or_uuid_to_datum(db_name, db_id, identifier_format);
    }
    if (db_name_out != nullptr) *db_name_out = db_name;
    return true;
}

bool convert_database_id_from_datum(
        const ql::datum_t &db_name_or_uuid,
        admin_identifier_format_t identifier_format,
        const cluster_semilattice_metadata_t &metadata,
        database_id_t *db_id_out,
        name_string_t *db_name_out,
        std::string *error_out) {
    if (identifier_format == admin_identifier_format_t::name) {
        name_string_t name;
        if (!convert_name_from_datum(db_name_or_uuid, "database name",
                                     &name, error_out)) {
            return false;
        }
        const_metadata_searcher_t<database_semilattice_metadata_t> searcher(
            &metadata.databases.databases);
        metadata_search_status_t search_status;
        auto it = searcher.find_uniq(name, &search_status);
        if (!check_metadata_status(
                search_status, "Database", name.str(), true, error_out)) {
            return false;
        }
        if (db_id_out != nullptr) *db_id_out = it->first;
        if (db_name_out != nullptr) *db_name_out = name;
        return true;
    } else {
        database_id_t db_id;
        if (!convert_uuid_from_datum(db_name_or_uuid, &db_id, error_out)) {
            return false;
        }
        auto it = metadata.databases.databases.find(db_id);
        if (it == metadata.databases.databases.end() || it->second.is_deleted()) {
            *error_out = strprintf("There is no database with UUID `%s`.",
                uuid_to_str(db_id).c_str());
            return false;
        }
        if (db_id_out != nullptr) *db_id_out = db_id;
        if (db_name_out != nullptr) *db_name_out = it->second.get_ref().name.get_ref();
        return true;
    }
}

ql::datum_t convert_port_to_datum(
        uint16_t value) {
    return ql::datum_t(static_cast<double>(value));
}

ql::datum_t convert_microtime_to_datum(
        microtime_t value) {
    return ql::pseudo::make_time(value / 1.0e6, "+00:00");
}

bool converter_from_datum_object_t::init(
        ql::datum_t _datum,
        std::string *error_out) {
    if (_datum.get_type() != ql::datum_t::R_OBJECT) {
        *error_out = "Expected an object; got " + _datum.print();
        return false;
    }
    datum = _datum;
    for (size_t i = 0; i < datum.obj_size(); ++i) {
        std::pair<datum_string_t, ql::datum_t> pair = datum.get_pair(i);
        extra_keys.insert(pair.first);
    }
    return true;
}

bool converter_from_datum_object_t::get(
        const char *key,
        ql::datum_t *value_out,
        std::string *error_out) {
    extra_keys.erase(datum_string_t(key));
    *value_out = datum.get_field(key, ql::NOTHROW);
    if (!value_out->has()) {
        *error_out = strprintf("Expected a field named `%s`.", key);
        return false;
    }
    return true;
}

void converter_from_datum_object_t::get_optional(
        const char *key,
        ql::datum_t *value_out) {
    extra_keys.erase(datum_string_t(key));
    *value_out = datum.get_field(key, ql::NOTHROW);
}

bool converter_from_datum_object_t::has(const char *key) {
    return datum.get_field(key, ql::NOTHROW).has();
}

bool converter_from_datum_object_t::check_no_extra_keys(std::string *error_out) {
    if (!extra_keys.empty()) {
        *error_out = "Unexpected key(s):";
        for (const datum_string_t &key : extra_keys) {
            (*error_out) += " " + key.to_std();
        }
        return false;
    }
    return true;
}

