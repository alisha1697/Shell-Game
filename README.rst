===================
Lab 5: A Shell Game
===================

.. contents:: Contents
    :depth: 2

TL;DR
=====

Deadlines
---------
See Canvas.

Summary
-------

In this lab, you will build a simple shell called ``crash`` — a (very) pared-back version of what you interact with when you log into a UNIX machine.

Shells allow users to start new processes by typing in commands with their arguments. They also receive certain signals — such as keyboard interrupts — and forward them to the relevant processes.

Although the code you need to write is fairly simple — we've covered spawning processes and catching signals in lecture — the devil lies in the fact that several things might be happening *concurrently*. For example, your code might be modifying some data structure when control is suddenly transferred to a signal handler, which might also want to modify that data structure. You'll also need to consider where inputs and signals end up when you spawn new processes.


Logistics
---------

As in the prior labs, we will use ``castor.ece.ubc.ca`` to evaluate your submissions, so your code must work there. All submissions are made by committing your code and pushing the commits to the main branch of the assignment repo on GitHub before the deadline for the relevant task(s).

As with all work in CPEN 212, everything you submit must be original and written by you from scratch after the lab has been released. You may not refer to any code other than what is found in the textbooks and lectures, and you must not share code with anyone at any time. See the academic integrity policy for details.

Be sure that you are submitting only **working code**. If your code does not compile, does not link, or crashes, you will receive **no credit** for the relevant task. It's much better to submit code with less functionality that is well-tested and works than code that has more features but does not compile or crashes.

Unlike in some other labs, we compile the source file you submit (``crash.c``) without linking it with any other test files (other than ``libc``); this means that your code must compile into an executable program.

Tasks are cumulative: for example, if you can't launch a job then it's not possible to determine if you can handle interrupting it.

As always, make sure you review the process and signal lectures and readings, and understand *everything*. And read the Hints sections; they're there for a reason.

Much of the documentation you will need is typically accessed via the ``man`` command at the Linux prompt, which brings up the manual page for a specific command or function. Here are some manual pages you might find useful:

- ``man 7 signal``
- ``man signal-safety``
- ``man execvp``
- ``man waitpid``
- ``man 2 kill``

Sometimes there are multiple manual pages for the same keyword in different "chapters". For example, ``kill`` is both a command you can type in at the shell prompt (section 1) and a system call (section 2), while ``signal`` is both a system call (section 2) and a concept (section 7). In those cases, you need the number to specify which one you mean.

Before starting the lab, experiment with the reference implementation to get a good idea of the functionality you will need to implement. It's a good idea to start by writing some automated tests and run them on the reference shell. A tool like ``expect`` that can control interactive programs would be perfect for this. 

After implementing each task, double-check with the specification and the reference implementation to make sure your code meets the specification. In this lab it's very easy to break things that worked previously when you implement a new feature.

When you're logged into ``castor``, check that you don't have unwanted processes running. You can do this using the ``ps`` command, and you can kill unwanted processes using the ``kill`` command. ECE computers have a per-user limit on the number of concurrent processes; if you exceed this (and the processes are still running), you may be unable to log in again, and will be at the mercy of ECE IT. Because you're creating new processes in this lab, it's easy to create lots of processes that continue to run, and get in trouble.



Specification
=============

Reference implementation
------------------------

We have provided a reference implementation of ``crash`` which you can run with ``~cpen212/Public/lab5/crash-ref``. You might find it useful in case you're not sure how things are supposed to work.


Prompt and parsing
------------------

The ``crash`` shell accepts inputs one line at a time from standard input. Each time a new line is being accepted, ``crash`` displays ``crash>`` followed by a single space (ASCII 32).

Each line consists of tokens deliminated by whitespace, ``&``, or ``;``. Spaces, tabs, ``&``, and ``;`` are not tokens (and are not commands). Multiple spaces/tabs are equivalent to one. Your implementation may limit what it considers whitespace to ASCII space and horizontal tabs (characters 32 and 9).

Both ``;`` and ``&`` terminate a command; the remainder of the line constitutes separate commands. Programs launched by commands terminated with ``&`` will run in the background.

For example, ``foo     bar;glurph&&quit``  has four commands: one that runs ``foo bar`` in the foreground, another that runs ``glurph`` in the background, an empty command (technically in the background), and the ``quit`` shell command.

You do not need to implement any quoting or character escape mechanisms that shells normally implement.


Commands
--------

The following commands are typed at the ``crash>`` prompt; pressing the Enter key executes the command.

All commands below must be supported by the time you've finished all tasks. In the following examples, job IDs, process IDs, and programs to run and their arguments, will of course all vary depending on the circumstances.

- ``quit`` takes no arguments and exits ``crash``.

- ``jobs`` lists the jobs currently managed by ``crash`` that have not terminated.

- ``nuke`` kills all jobs in this shell with the KILL signal.

- ``nuke 12345`` kills process 12345 with the KILL signal if and only if it is a job in this shell that has not yet exited.

- ``nuke %7`` kills job %7 with the KILL signal if and only if it is a job in this shell that has not yet exited.

- ``fg %7`` puts job 7 in the foreground, resuming it if suspended.

- ``fg 12345`` puts process 12345 in the foreground, resuming it if suspended.

- ``bg %7`` resumes job 7 in the background if it is suspended.

- ``bg 12345`` resumes process 12345 in the background if it it suspended.

- ``nuke`` and ``bg`` may take multiple arguments, each of which can be either a PID or a job ID; e.g., ``nuke 12345 %7 %5 32447``.

- ``foo bar glurph`` runs the program ``foo`` with arguments ``bar`` and ``glurph`` in the foreground, inheriting the current environment.

- ``foo bar glurph &`` runs the program ``foo`` with arguments ``bar`` and ``glurph`` in the background, inheriting the current environment.

Separate commands may be separated with newlines, ``;``, or ``&``, so ``jobs ; quit`` or ``foo bar & quit`` each have two separate commands. Empty commands (i.e., commands that consist of no tokens) have no effect. Although ``;`` is just a separator, it can at first sigh appear to behave differently than ``&``; for example:

- ``foo & bar`` runs the program ``foo`` in the background and immediately ``bar`` in the foreground.

- ``foo ; bar &`` runs the program ``foo`` the foreground, waits for ``foo`` to finish (or be suspended), and then runs ``bar`` in the background.

Commands that identify a job or a process (``fg``, ``bg``, and ``nuke``) **only work if the job or process was launched from the current shell** (i.e., they do not work on external processes). Sending *any* signals to a process not spawned by the current instance of your shell is considered **incorrect behaviour.**

Commands that launch programs search the current PATH for the program binary (e.g., ``ls`` should run ``/bin/ls`` if ``/bin`` is first in your PATH).

Your shell must spawn no processes other than those caused by the above commands.


Job numbers and PIDs
--------------------

Jobs are launched with sequential job numbers starting at 1 (including jobs that failed to *execute*). You must support job IDs up to `INT_MAX`; we will not execute more commands than that in one session. Note that:

- zero is not a valid job number, and

- no two concurrently running jobs may have the same job number.

Your implementation must support up to 32 *concurrent* jobs.

Process IDs you display must match the PID assigned by the OS.


Messages
--------

All non-error messages printed by ``crash`` go to **standard output** (*not* to standard error). If any processes you start write to the standard output, they must write to the same standard output as ``crash``.

Each message must be produced **atomically**, so for example if the two messages are ``foo`` and ``bar`` then ``foboar`` would not be acceptable.

In all the examples below, the job IDs, process IDs, and programs being run (``sleep``) are for illustration purposes and will vary to match the circumstances.

- The ``jobs`` command shows the jobs currently in existence (i.e., running or suspended), one job per line. Each line shows the job number (1 and 2 in the example below), process IDs (12345 and 12346 in the example below), the status (``running`` or ``suspended``), and the command being run without its arguments (``sleep`` below). The jobs may appear in any order.

- When a job is placed in the background, either via the ``bg`` command or by starting the process with a command terminated by ``&``, ``crash`` prints::

        [1] (12345)  running  sleep

  A job is considered started if its process has been created.

- When a job terminates normally (not because of a signal), ``crash`` prints::

        [2] (12345)  finished  sleep

- When a job is suspended by sending STOP or TSTP signals (whether by pressing :kbd:`Ctrl+Z` for a foreground job or via an explicit signal), ``crash`` prints::

        [2] (12345)  suspended  sleep

- When a suspended job resumes execution, ``crash`` prints::

        [2] (12345)  continued  sleep

- When a job is terminated by any signal (e.g., by pressing :kbd:`Ctrl+C` or :kbd:`Ctrl+\\` for a foreground job, a segfault, etc.), ``crash`` prints one of these two messages, depending on whether the process also dumped core::

        [1] (12345)  killed  sleep
        [1] (12345)  killed (core dumped)  sleep

  Typically signals like SIGQUIT (:kbd:`Ctrl+\\`) or SIGSEGV cause the process to dump core, while signals like SIGINT (:kbd:`Ctrl+C`) don't.

Note the double spaces before the status and the command names in all cases; you must preserve these exactly.

All commands are displayed without arguments, but with any path that was provided when the command was started. For example, if you ran the command ``sleep 10 &`` you might see::

        [1] (12345)  running  sleep

but if you ran ``/usr/bin/sleep 10&`` you might see::

        [1] (12345)  running  /usr/bin/sleep


Errors
------

All errors printed by ``crash`` go to **standard error** (*not* to standard output). If any processes you start write to the standard output, they must write to the same standard output as ``crash``.

The ``quit`` and ``jobs`` commands can print the following error:

- ``ERROR: quit takes no arguments`` if the command receives arguments (mutatis mutandis).

The ``fg`` command can print this error:

- ``ERROR: fg needs exactly one argument`` if there are two or more arguments.

The ``bg`` command can print this error:

- ``ERROR: bg needs some arguments`` if there are no arguments.

Commands that take process ID or job number arguments (``nuke``, ``fg``, and ``bg``) can also print several kinds of errors:

- ``ERROR: bad argument for fg: %133t`` if the job ID cannot be parsed as a valid job ID (mutatis mutandis). Note that PID 0 is valid but job ID 0 is not.

- ``ERROR: bad argument for fg: 133t`` if the process ID cannot be parsed as an integer (mutatis mutandis).

- ``ERROR: no job 1337`` if the shell has no running or suspended job with the given job ID.

- ``ERROR: no PID 1337`` if the shell has no running or suspended job with the given process ID.

When multiple arguments are allowed (``nuke`` and ``bg``), these errors are printed for every argument that causes them; the remaining arguments are still processed. For example, if no jobs exist, ``bg %17; fg %23`` prints::

    ERROR: no job 17
    ERROR: no job 23

Commands that launch programs can print the following error:

- ``ERROR: cannot run foo`` (mutatis mutandis) if the program ``foo`` cannot be executed for any reason (e.g., not found on path, no permissions, can't spawn a new process, etc). The error message does *not* include the arguments passed to the program.

- ``ERROR: too many jobs`` if there are already 32 jobs running on suspended when a command to start another job is issued (in which case the new job does not start).


Keyboard inputs
---------------

Most inputs go to the shell, but are accepted only when no foreground job is running (they may be buffered by the kernel and ``libc``). This means that you don't need to worry about processes that accept inputs themselves; for example, running ``cat`` does not need to work.

Keyboard inputs that normally raise signals or close the input stream behave as follows, assuming default ``stty`` settings for which keys do what:

- :kbd:`Ctrl+C` kills the foreground process (if any) via the SIGINT signal. If there is no foreground process, :kbd:`Ctrl+C` is ignored.

- :kbd:`Ctrl+Z` suspends the foreground process (if any) via the SIGTSTP signal. If there is no foreground process, :kbd:`Ctrl+Z` is ignored.

- :kbd:`Ctrl+\\` sends SIGQUIT to the foreground process (if any). If there is no foreground process, exits ``crash`` with exit status 0.

- :kbd:`Ctrl+D` is ignored if there is a foreground process; otherwise it exits ``crash`` with exit status 0.



Coding
======

Template
--------

We've provided a template of ``crash.c`` in each task directory. We have already implemented the annoying but boring command parsing bit for you, as well as the ``quit`` command.

For each task, you will need to replace ``crash.c`` file with the implementation that satisfies the relevant task requirements.


Rules
-----

Some constraints you must obey when writing code:

- When compiling your code, we will only use ``crash.c`` in the relevant directory. This means that all your code must be in ``crash.c``.

- Your code must link into a complete program (that is, it must have a ``main``).

- Your code must be in C (specifically the dialect used by default by the globally-installed ``gcc`` on ``castor``).

- Your code must not require linking against any libraries other that the usual ``libc`` (which is linked against by default when compiling C).

- Needless to say, your code must compile and run without errors. If we can't compile or run your code, you will receive no credit for the relevant task.

If you violate these rules, we will likely not be able to compile and/or properly test your code, and you will receive no credit for the relevant task(s).



Task 1
======

When a shell runs a *background* job, control returns to the shell, and any keys you press go to the shell. The shell displays the prompt immediately, and you can issue more shell commands; keystrokes that would normally send signals to the process (e.g., :kbd:`Ctrl+C`) send them to the shell instead.

Required functionality:

- Typing a command name with arguments and ``&`` at the end should spawn a new process with the command / args, as specified.

- For this task, at most 32 job processes may be started (you don't know if any processes have terminated, so you have to consider all jobs concurrent).

- The ``quit`` command should work as specified.

- :kbd:`Ctrl+D` should work as specified.


Deliverables
------------

In ``task1``:

- ``crash.c``


Hints
-----

- How do you search the PATH for the executable you want? ``execvp`` is a wrapper for the ``execve`` system call that does just that. ``man execvp`` for more info.

- Remember to mask and unmask signals appropriately when you fork and modify any data structures to avoid race conditions.

- When you can't run some command, make sure you don't leave extra copies of ``crash`` running instead.

- Check the messages and errors specification and the reference shell to make sure you produce the correct message when your job starts, and so on.

- The ``sleep`` program is quite useful for testing throughout this lab, because it runs for a specified number of seconds and then finishes.

- If you do use ``sleep``, don't make the time too long, or you might hit the per-user process limit.

- Learn to automate your tests. It's worth it.



Task 2
======

In this task, you will implement the ``jobs`` command that describes the status of jobs you've started inside ``crash``. This means you need to implement a data structure for tracking these jobs.

Required functionality in addition to previous tasks:

- The ``jobs`` command should display all jobs that have been started, as in the spec.

- For this task:

  - at most 32 job processes may be started (since you still don't know if any processes have terminated), and
  
  - the job list includes any jobs for which a process has been started (for the same reason).


Deliverables
------------

In ``task2``:

- ``crash.c``


Hints
-----

- Remember to mask and unmask signals appropriately when you fork and modify any data structures to avoid race conditions.

- Check the specification and the reference shell for any messages and errors you need to implement.

- You will likely want to define a ``struct`` that represents a single job, so it is easy to extend later.

- If you create any job tracking structures, consider that you will need to access them from signal handlers, which can only run signal-safe functions.

- Remember that the contents of ``toks`` will change the next time ``crash`` parses another command.



Task 3
======

A job spawned by the shell could *terminate* -- either because it simply finished its work or because it crashed. The only way for the shell to know this is by being notified via the SIGCHLD signal. In this task, you will partially implement the signal handler for SIGCHLD.

Required functionality in addition to previous tasks:

- The shell must correctly handle to the SIGCHLD signal *when the child has terminated* in any way.

- Once a job has terminated, it should never again appear in the output of ``jobs``.

- The messages specified for jobs that have terminated (either finished or died because of a signal) must be implemented, including the core dump annotation.

- The ``nuke`` command must be implemented as specified.


Deliverables
------------

In ``task3``:

- ``crash.c``


Hints
-----

- Check the specification to make sure the outputs for ``jobs`` and all the messages are *exactly* correct. We will test this automatically so if you use a different format our marking code will not accept it.

- Make sure there are no data races when accessing shared data structures. Remember signals can occur at any time.

- Carefully read the manual page for ``waitpid`` (``man waitpid``) and go through the lecture examples.

- Recall from lecture that signals are *not queued*, so you *might not* receive a separate SIGCHLD for every process that has terminated.

- Signals can be sent to other processes via the ``kill`` system call. Run ``man 2 kill`` to see its manual page.

- Note that ``nuke`` can take any number of arguments (including none), and any arguments can be either job IDs or process IDs. Be sure to implement *all variants*.

- Many useful functions are *unsafe* in signal handlers; ``man signal-safety`` for details.

  - In particular, memory allocation/freeing, most printing functions, etc., are **not signal-safe**. However, ``write`` and ``strlen`` *are* signal-safe.

  - You can call these functions *outside* the signal handlers, though, if you wish — for example, you could compute useful things when you first spawn the job and store them somewhere.

  - If you call any function that might modify ``errno``, you need to save ``errno`` at signal handler entry time and restore it at exit time.

- Think about where you want to print any output. Many actions you implement here and in later tasks work by sending signals to processes, but those signals can also be received from another source; make sure the messages correspond to the spec / reference implementation.


Task 4
======

In contrast to the *background* job mechanism you've already implemented, a *foreground* job blocks the shell from accepting any inputs.

Your implementation must wait for the foreground job to finish before displaying the prompt and accepting more commands. Keystrokes that send signals send them to the foreground job; all other inputs go to the shell, but are not processed until there is no foreground job.

At any time, there may be either exactly one foreground job or no foreground jobs.

Required functionality in addition to previous tasks:

- Jobs started without the trailing ``&`` must pause the shell until they terminate or are suspended.

- The SIGINT and SIGQUIT signals (whether sent via :kbd:`Ctrl+C` and :kbd:`Ctrl+\\` or received externally) must operate as specified *both* when there *is* a foreground job and when there is *no* foreground job.

- When no foreground job is running, issuing the ``fg`` command with a valid job ID or process ID must make the relevant background job a foreground job.


Deliverables
------------

In ``task4``:

- ``crash.c``


Hints
-----

- How do you pause the shell? What you can do is wait in one place until a signal terminates or stops the foreground job. A spin-loop is one way to do this, but it's crazily inefficient; see below for better ideas.

- There is a ``pause`` function call that waits until some signal is received. But you can't use it because you could run into a race condition: if the child quits, you might receive a SIGCHLD for it *before* ``pause`` starts, and then the ``pause`` would never finish.

- The easiest thing is to use ``sleep`` (or ``usleep``) instead, as they also return when a signal is received. As usual, use ``man`` to read the manual pages. If you do this, be sure to sleep for *no more than 1ms at a time*.

- ``sleep`` will return when *any* signal is received, but this might not be a signal for the foreground job.

- Carefully consider where any such pauses should be implemented. In particular, think about ``sleep 1;sleep 1`` behaves.

- The ``kill`` system call can send any signal to a process, not just SIGKILL. In particular you will need to forward some signals to a foreground child process if there is one.

- For this task you don't need to handle the case when the foreground job is *suspended*, just terminated. Suspended jobs are in the next task.

- Forking duplicates the entire process, including the open file descriptors; this includes standard input, which can result in a race condition. Luckily in this lab you don't need to send any inputs to processes you spawn (other than the specified signals) so you can just close standard input.

- Signals caused by events like a :kbd:`Ctrl+Z` are sent to the entire *process group* with the same process group ID as the current process. By default, a child inherits its parent's process group ID, so you'll want to change this with ``setpgid``.

- Make sure to check the specification and the reference shell that you've implemented any messages and errors correctly.



Task 5
======

Not surprisingly, a *suspended* job is one that is not currently running, unlike background and foreground jobs. Suspended jobs may be restarted either in the foreground or the background, or they can be terminated.

Jobs can be paused by receiving the SIGSTOP or SIGTSTP signals (the latter of which can be sent via :kbd:`Ctrl+Z` to a foreground process or externally), and resumed by receiving the SIGCONT signal.

Required functionality in addition to previous tasks:

- The SIGTSTP signal must work as specified, whether sent by :kbd:`Ctrl+Z` to a foreground job or externally to a foreground or background job.

- Running ``fg`` or ``bg`` commands that specify a suspended job will resume the job and place it in the foreground or background depending on the command.

- Processes resumed by receiving SIGCONT from an external source continue as if resumed by the ``bg`` command.

- The ``jobs`` command must reflect whether each job is running or suspended, as specified.


Deliverables
------------

In ``task5``:

- ``crash.c``


Hints
-----

- Read ``man waitpid`` again, especially the section about ``wstatus``. This allows you to determine whether the relevant child was terminated or suspended.

- To resume a suspended job, you can send a SIGCONT signal to it via the ``kill`` function.

- Be sure that jobs that are resumed as foreground cause the shell to pause as if they were launched without ``&``.

- Check the specification and the reference shell to make sure you've correctly implemented any messages or errors.



Marks
=====

To earn marks, you must commit and push each task to the GitHub repo **before the deadline for that task**.

Remember that CPEN 212 labs are **individual**, so you must complete all tasks by yourself; see the academic integrity policy for details.

- Task 1: 2
- Task 2: 2
- Task 3: 2
- Task 4: 2
- Task 5: 2

We test features incrementally, so the tests for later tasks rely on previous tasks working (with the exception of task 1).
