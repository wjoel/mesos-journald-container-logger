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

// Based on mesos/src/slave/container_loggers/sandbox.hpp
// Modifications copyright (C) 2016 Joel Wilsson

#ifndef __JOURNALD_CONTAINER_LOGGER_HPP__
#define __JOURNALD_CONTAINER_LOGGER_HPP__

#include <mesos/mesos.hpp>

#include <mesos/slave/container_logger.hpp>

#include <process/future.hpp>
#include <process/owned.hpp>
#include <process/subprocess.hpp>

#include <stout/try.hpp>
#include <stout/nothing.hpp>
#include <stout/option.hpp>

class JournaldLoggerProcess;

using namespace mesos;

// Executors and tasks launched through this container logger will have their
// stdout and stderr sent to journald.
class JournaldContainerLogger : public mesos::slave::ContainerLogger
{
public:
  JournaldContainerLogger();
  virtual ~JournaldContainerLogger();

  virtual Try<Nothing> initialize();

  virtual process::Future<Nothing> recover(
      const ExecutorInfo& executorInfo,
      const std::string& sandboxDirectory);

  virtual process::Future<mesos::slave::ContainerLogger::SubprocessInfo>
  prepare(
      const ExecutorInfo& executorInfo,
      const std::string& sandboxDirectory);

protected:
  process::Owned<JournaldLoggerProcess> process;
};

#endif // __JOURNALD_CONTAINER_LOGGER_HPP__
