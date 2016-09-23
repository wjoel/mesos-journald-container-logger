// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Based on mesos/src/tests/container_logger_tests.cpp
// and mesos/src/tests/module_tests.cpp
// Modifications copyright (C) 2016 Joel Wilsson

#include <list>
#include <string>
#include <cstdio>
#include <cstring>
#include <vector>

#include <systemd/sd-journal.h>

#include <mesos/module.hpp>

#include <gmock/gmock.h>

#include <process/clock.hpp>
#include <process/future.hpp>
#include <process/gtest.hpp>
#include <process/owned.hpp>

#include <stout/bytes.hpp>
#include <stout/gtest.hpp>
#include <stout/dynamiclibrary.hpp>
#include <stout/os.hpp>
#include <stout/path.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>

#include <stout/os/exists.hpp>
#include <stout/os/killtree.hpp>
#include <stout/os/mkdir.hpp>
#include <stout/os/pstree.hpp>
#include <stout/os/read.hpp>
#include <stout/os/stat.hpp>

#include "master/master.hpp"

#include "module/manager.hpp"
#include "module/container_logger.hpp"

#include "slave/flags.hpp"
#include "slave/paths.hpp"
#include "slave/slave.hpp"

#include "slave/containerizer/docker.hpp"
#include "slave/containerizer/fetcher.hpp"

#include "slave/containerizer/mesos/containerizer.hpp"

#include "slave/containerizer/mesos/provisioner/provisioner.hpp"

#include "tests/flags.hpp"
#include "tests/mesos.hpp"
#include "tests/utils.hpp"

#include "tests/containerizer/launcher.hpp"

using namespace process;

using mesos::internal::master::Master;

using mesos::internal::slave::Fetcher;
using mesos::internal::slave::Launcher;
using mesos::internal::slave::MesosContainerizer;
using mesos::internal::slave::PosixLauncher;
using mesos::internal::slave::Provisioner;
using mesos::internal::slave::Slave;

using mesos::internal::slave::state::ExecutorState;
using mesos::internal::slave::state::FrameworkState;
using mesos::internal::slave::state::RunState;
using mesos::internal::slave::state::SlaveState;

using mesos::internal::SlaveRegisteredMessage;

using mesos::master::detector::MasterDetector;

using mesos::slave::ContainerLogger;
using mesos::slave::Isolator;

using mesos::modules::ModuleBase;
using mesos::modules::ModuleManager;

using std::list;
using std::string;
using std::vector;

using testing::_;
using testing::AtMost;
using testing::Return;

using namespace mesos::internal::tests;

using namespace mesos;


namespace journald_container_logger_test {

const char* DEFAULT_MODULE_LIBRARY_NAME = "./libmesos_journald_container_logger.so";
const char* DEFAULT_MODULE_NAME = "com_wjoel_JournaldContainerLogger";

class JournaldContainerLoggerTest : public MesosTest
{
protected:
  static void SetUpTestCase()
  {
    EXPECT_SOME(dynamicLibrary.open(DEFAULT_MODULE_LIBRARY_NAME));

    Try<void*> symbol = dynamicLibrary.loadSymbol(DEFAULT_MODULE_NAME);
    EXPECT_SOME(symbol);

    moduleBase = static_cast<ModuleBase*>(symbol.get());
  }

  static void TearDownTestCase()
  {
    MesosTest::TearDownTestCase();

    // Close the module library.
    dynamicLibrary.close();
  }

  JournaldContainerLoggerTest()
    : module(None())
  {
    Modules::Library* library = modules.add_libraries();
    library->set_file(DEFAULT_MODULE_LIBRARY_NAME);
    library->add_modules()->set_name(DEFAULT_MODULE_NAME);
  }

  // During the per-test tear-down, we unload the module to allow
  // later loads to succeed.
  ~JournaldContainerLoggerTest()
  {
    // The TestModule instance is created by calling new. Let's
    // delete it to avoid memory leaks.
    if (module.isSome()) {
      delete module.get();
    }

    // Reset module API version and Mesos version in case the test
    // changed them.
    moduleBase->kind = "ContainerLogger";
    moduleBase->moduleApiVersion = MESOS_MODULE_API_VERSION;
    moduleBase->mesosVersion = MESOS_VERSION;

    // Unload the module so a subsequent loading may succeed.
    ModuleManager::unload(DEFAULT_MODULE_NAME);
  }

  Modules modules;
  Result<ContainerLogger*> module;

  static DynamicLibrary dynamicLibrary;
  static ModuleBase* moduleBase;
};

DynamicLibrary JournaldContainerLoggerTest::dynamicLibrary;
ModuleBase* JournaldContainerLoggerTest::moduleBase = nullptr;

// Tests that the default container logger writes files into the sandbox.
TEST_F(JournaldContainerLoggerTest, LogToJournald)
{
  EXPECT_SOME(ModuleManager::load(modules));
  module = ModuleManager::create<ContainerLogger>("com_wjoel_JournaldContainerLogger");
  EXPECT_SOME(module);

  // Create a master, agent, and framework.
  Try<Owned<cluster::Master>> master = StartMaster();
  ASSERT_SOME(master);

  Future<SlaveRegisteredMessage> slaveRegisteredMessage =
    FUTURE_PROTOBUF(SlaveRegisteredMessage(), _, _);

  // We'll need access to these flags later.
  mesos::internal::slave::Flags flags = CreateSlaveFlags();
  flags.container_logger = "com_wjoel_JournaldContainerLogger";

  Fetcher fetcher;

  // We use an actual containerizer + executor since we want something to run.
  Try<MesosContainerizer*> _containerizer =
    MesosContainerizer::create(flags, false, &fetcher);

  CHECK_SOME(_containerizer);
  Owned<MesosContainerizer> containerizer(_containerizer.get());

  Owned<MasterDetector> detector = master.get()->createDetector();

  Try<Owned<cluster::Slave>> slave =
    StartSlave(detector.get(), containerizer.get(), flags);
  ASSERT_SOME(slave);

  AWAIT_READY(slaveRegisteredMessage);
  SlaveID slaveId = slaveRegisteredMessage.get().slave_id();

  MockScheduler sched;
  MesosSchedulerDriver driver(
      &sched, DEFAULT_FRAMEWORK_INFO, master.get()->pid, DEFAULT_CREDENTIAL);

  Future<FrameworkID> frameworkId;
  EXPECT_CALL(sched, registered(&driver, _, _))
    .WillOnce(FutureArg<1>(&frameworkId));

  // Wait for an offer, and start a task.
  Future<vector<Offer>> offers;
  EXPECT_CALL(sched, resourceOffers(&driver, _))
    .WillOnce(FutureArg<1>(&offers))
    .WillRepeatedly(Return()); // Ignore subsequent offers.

  driver.start();
  AWAIT_READY(frameworkId);

  AWAIT_READY(offers);
  EXPECT_NE(0u, offers.get().size());

  sd_journal *j;
  EXPECT_GE(0, sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY));

  // We'll start a task that outputs to stdout.
  TaskInfo task = createTask(offers.get()[0], "echo 'mesos.hello-world!'");

  Future<TaskStatus> statusRunning;
  Future<TaskStatus> statusFinished;
  EXPECT_CALL(sched, statusUpdate(&driver, _))
    .WillOnce(FutureArg<1>(&statusRunning))
    .WillOnce(FutureArg<1>(&statusFinished))
    .WillRepeatedly(Return());       // Ignore subsequent updates.

  driver.launchTasks(offers.get()[0].id(), {task});

  AWAIT_READY(statusRunning);
  EXPECT_EQ(TASK_RUNNING, statusRunning.get().state());

  AWAIT_READY(statusFinished);
  EXPECT_EQ(TASK_FINISHED, statusFinished.get().state());

  driver.stop();
  driver.join();

  slave->reset();

  bool found = false;
  SD_JOURNAL_FOREACH(j) {
    const char *d;
    size_t l;

    if (sd_journal_get_data(j, "MESSAGE", (const void **)&d, &l) < 0) {
      //fprintf(stderr, "Failed to read message field: %s\n", strerror(-r));
      printf("Failed to read message");
      continue;
    }

    if (strncmp(d, "MESSAGE=mesos.hello-world!", l) == 0) {
      found = true;
    }
  }
  sd_journal_close(j);
  ASSERT_TRUE(found);
}
} // namespace journald_container_logger_test {
