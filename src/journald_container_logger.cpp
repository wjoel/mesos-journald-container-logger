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

// Based on mesos/src/slave/container_loggers/sandbox.cpp
// Modifications copyright (C) 2016 Joel Wilsson

#include <string>

#include <syslog.h>
#include <systemd/sd-journal.h>

#include <mesos/mesos.hpp>
#include <mesos/module.hpp>
#include <mesos/module/container_logger.hpp>

#include <process/dispatch.hpp>
#include <process/future.hpp>
#include <process/id.hpp>
#include <process/process.hpp>

#include <stout/try.hpp>
#include <stout/nothing.hpp>
#include <stout/option.hpp>

#include "journald_container_logger.hpp"

using namespace process;
using namespace mesos;
using mesos::slave::ContainerLogger;

using SubprocessInfo = ContainerLogger::SubprocessInfo;

class JournaldLoggerProcess :
  public Process<JournaldLoggerProcess>
{
private:
  int journal_out = -1;
  int journal_err = -1;

public:
  JournaldLoggerProcess()
    : ProcessBase(process::ID::generate("journald-logger")) {}

  virtual ~JournaldLoggerProcess()
  {
    if (journal_out > 0) {
      close(journal_out);
    }
    if (journal_err > 0) {
      close(journal_err);
    }
  }

  Future<Nothing> recover(
      const ExecutorInfo& executorInfo,
      const std::string& sandboxDirectory)
  {
    return Nothing();
  }

  process::Future<ContainerLogger::SubprocessInfo> prepare(
      const ExecutorInfo& executorInfo,
      const std::string& sandboxDirectory)
  {
    ContainerLogger::SubprocessInfo info;

    // Although somewhat specific to Marathon, we look for the environment
    // variable MESOS_TASK_ID and use that as the journal identifier
    // if we find it.
    std::string identifier;
    if (executorInfo.has_command() &&
        executorInfo.command().has_environment()) {
      foreach (const Environment::Variable variable,
               executorInfo.command().environment().variables()) {
        if (variable.name() == "MESOS_TASK_ID") {
          identifier = variable.value();
          break;
        }
      }
    }

    journal_out = sd_journal_stream_fd(identifier.c_str(), LOG_INFO, 1);
    journal_err = sd_journal_stream_fd(identifier.c_str(), LOG_ERR, 1);

    info.out = SubprocessInfo::IO::FD(journal_out);
    info.err = SubprocessInfo::IO::FD(journal_err);

    return info;
  }
};


JournaldContainerLogger::JournaldContainerLogger()
  : process(new JournaldLoggerProcess())
{
  spawn(process.get());
}


JournaldContainerLogger::~JournaldContainerLogger()
{
  terminate(process.get());
  wait(process.get());
}


Try<Nothing> JournaldContainerLogger::initialize()
{
  return Nothing();
}


Future<Nothing> JournaldContainerLogger::recover(
    const ExecutorInfo& executorInfo,
    const std::string& sandboxDirectory)
{
  return dispatch(
      process.get(),
      &JournaldLoggerProcess::recover,
      executorInfo,
      sandboxDirectory);
}

Future<ContainerLogger::SubprocessInfo>
JournaldContainerLogger::prepare(
    const ExecutorInfo& executorInfo,
    const std::string& sandboxDirectory)
{
  return dispatch(
      process.get(),
      &JournaldLoggerProcess::prepare,
      executorInfo,
      sandboxDirectory);
}

static ContainerLogger* create(const Parameters& parameters) {
  return new JournaldContainerLogger();
}

static bool compatible()
{
  return true;
}

mesos::modules::Module<ContainerLogger>
com_wjoel_JournaldContainerLogger(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Joel Wilsson",
    "joel.wilsson@gmail.com",
    "Journald Container Logger module.",
    compatible,
    create);
