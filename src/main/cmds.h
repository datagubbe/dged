struct commands;

void register_global_commands(struct commands *commands,
                              void (*terminate_cb)());
void teardown_global_commands(void);

void register_buffer_commands(struct commands *commands);

void register_window_commands(struct commands *commands);

void register_settings_commands(struct commands *commands);
