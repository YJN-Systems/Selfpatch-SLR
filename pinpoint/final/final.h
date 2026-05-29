#pragma once

void on_finish_unit(void *plugin_data, void *user_data);

void set_output_file(const char *path);
bool has_output_file();
const char *get_output_file();

bool init_src_file();
const char *get_src_file();

bool init_cu_hash();
const char *get_cu_hash();
