#include "assert.h"
#include "test.h"

#include "command.h"

void test_command_registry_create() {
  struct commands cmds = command_registry_create(10);

  ASSERT(cmds.capacity == 10, "Expected capacity to be the specified value");
  ASSERT(cmds.ncommands == 0,
         "Expected number of commands to initially be empty");

  command_registry_destroy(&cmds);
}

int32_t fake_command(struct command_ctx ctx, int argc, const char *argv[]) {
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

void test_register_command() {
  struct commands cmds = command_registry_create(1);

  struct command cmd = {
      .fn = fake_command,
      .name = "fake",
      .userdata = NULL,
  };
  register_command(&cmds, cmd);

  ASSERT(cmds.ncommands == 1,
         "Expected number of commands to be 1 after inserting one");

  struct command multi[] = {
      {.fn = fake_command, .name = "fake1", .userdata = NULL},
      {.fn = fake_command, .name = "fake2", .userdata = NULL},
  };

  register_commands(&cmds, multi, 2);
  ASSERT(cmds.ncommands == 3,
         "Expected number of commands to be 3 after inserting two more");
  ASSERT(cmds.capacity > 1,
         "Expected capacity to have increased to accommodate new commands");
}

void test_lookup_command() {
  struct commands cmds = single_fake_command("fake");
  struct command *cmd = lookup_command(&cmds, "fake");

  ASSERT(cmd != NULL,
         "Expected to be able to look up inserted command by name");
  ASSERT_STR_EQ(cmd->name, "fake",
                "Expected the found function to have the correct name");

  struct command *also_cmd =
      lookup_command_by_hash(&cmds, hash_command_name("fake"));
  ASSERT(cmd != NULL,
         "Expected to be able to look up inserted command by hash");
  ASSERT_STR_EQ(cmd->name, "fake",
                "Expected the found function to have the correct name");
}

int32_t failing_command(struct command_ctx ctx, int argc, const char *argv[]) {
  return 100;
}

void test_execute_command() {
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
}

void run_command_tests() {
  run_test(test_command_registry_create);
  run_test(test_register_command);
  run_test(test_lookup_command);
  run_test(test_execute_command);
}
