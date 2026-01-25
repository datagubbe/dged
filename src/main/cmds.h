struct commands;
struct frame_allocator;

void register_global_commands(struct commands *commands,
                              void (*terminate_cb)(void),
                              void (*suspend_cb)(void),
                              struct frame_allocator *alloc);
void teardown_global_commands(void);

void register_buffer_commands(struct commands *commands);

void register_window_commands(struct commands *commands);

void register_settings_commands(struct commands *commands);
