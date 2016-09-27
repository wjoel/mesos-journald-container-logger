# mesos-journald-container-logger

This is a container logger module for [Mesos](http://mesos.apache.org/)
that redirects container logs to the systemd journal, or "journald".

Messages written to the container's standard output are logged using the
priority level `LOG_INFO`, and standard error messages are logged using
the priority level `LOG_ERR`.

If the environment variable `MESOS_TASK_ID` is defined its value will be
used as the program identifier in the log messages. This is mostly useful
if Marathon is used to launch the tasks.

## Tests

You (or the user running the tests) need to to have access to the systemd
journal, or they will fail. Access is usually granted to members of the
`systemd-journal` group. To add jenkins to this group, for example:

    $ usermod -a -G systemd-journal jenkins

Normal users may need to login again, and you may need to restart services
such as jenkins, for the group changes to be applied.
