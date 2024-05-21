#include "assert.h"
#include "test.h"

#include "dged/command.h"
#include "dged/hash.h"
#include "dged/hashmap.h"

void test_command_registry_create(void) {
  struct commands cmds = command_registry_create(10);

  ASSERT(HASHMAP_CAPACITY(&cmds.commands) == 10,
         "Expected capacity to be the specified value");
  ASSERT(HASHMAP_SIZE(&cmds.commands) == 0,
         "Expected number of commands to initially be empty");

  command_registry_destroy(&cmds);
}

int32_t fake_command(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)ctx;
  (void)argc;
  (void)argv;

  return 0;
}

struct commands single_fake_command(const char *name) {
  struct commands cmds = command_registry_create(10);

  struct command cmd = {
      .fn = fake_command,
      .name = name,
      .userdata = NULL,
  };
  register_command(&cmds, cmd);

  return cmds;
}

void test_register_command(void) {
  struct commands cmds = command_registry_create(1);

  struct command cmd = {
      .fn = fake_command,
      .name = "fake",
      .userdata = NULL,
  };
  register_command(&cmds, cmd);

  ASSERT(HASHMAP_SIZE(&cmds.commands) == 1,
         "Expected number of commands to be 1 after inserting one");

  struct command multi[] = {
      {.fn = fake_command, .name = "fake1", .userdata = NULL},
      {.fn = fake_command, .name = "fake2", .userdata = NULL},
  };

  register_commands(&cmds, multi, 2);
  ASSERT(HASHMAP_SIZE(&cmds.commands) == 3,
         "Expected number of commands to be 3 after inserting two more");
  ASSERT(HASHMAP_CAPACITY(&cmds.commands) > 1,
         "Expected capacity to have increased to accommodate new commands");

  command_registry_destroy(&cmds);
}

void test_lookup_command(void) {
  struct commands cmds = single_fake_command("fake");
  struct command *cmd = lookup_command(&cmds, "fake");

  ASSERT(cmd != NULL,
         "Expected to be able to look up inserted command by name");
  ASSERT_STR_EQ(cmd->name, "fake",
                "Expected the found function to have the correct name");

  ASSERT(cmd != NULL,
         "Expected to be able to look up inserted command by hash");
  ASSERT_STR_EQ(cmd->name, "fake",
                "Expected the found function to have the correct name");

  command_registry_destroy(&cmds);
}

int32_t failing_command(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)ctx;
  (void)argc;
  (void)argv;

  return 100;
}

void test_execute_command(void) {
  struct commands cmds = single_fake_command("fake");
  struct command *cmd = lookup_command(&cmds, "fake");

  int32_t res = execute_command(cmd, &cmds, NULL, NULL, 0, NULL);
  ASSERT(res == 0, "Expected to be able to execute command successfully");

  register_command(&cmds, (struct command){
                              .fn = failing_command,
                              .name = "fejl",
                              .userdata = NULL,
                          });
  struct command *fail_cmd = lookup_command(&cmds, "fejl");
  int32_t res2 = execute_command(fail_cmd, &cmds, NULL, NULL, 0, NULL);
  ASSERT(res2 != 0, "Expected failing command to fail");

  command_registry_destroy(&cmds);
}

void run_command_tests(void) {
  run_test(test_command_registry_create);
  run_test(test_register_command);
  run_test(test_lookup_command);
  run_test(test_execute_command);
}
