# tsh-mini-shell 🖥️
 University project making a replica mini shell command prompt using C


# Tiny Shell (tsh) - Unix Shell Implementation

## Introduction
In this project, I developed a miniature Unix shell program called `tsh` (Tiny Shell) to gain a deeper understanding of process control, signals, and pipes. The shell supports running external programs, managing job states, handling input/output redirection, processing signals, and creating pipelines. Key functionalities include parsing and interpreting commands, recognizing built-in commands like `quit`, `fg`, `bg`, and `jobs`, and handling signals such as SIGINT (Ctrl-C) and SIGTSTP (Ctrl-Z).

## Design and Implementation
The `tsh` shell was built using a structured approach where core functions were implemented to manage process creation, job control, and signal handling. The `eval` function parses commands and handles input/output redirection and background jobs. Built-in commands were managed using the `builtin_cmd` and `do_bgfg` functions. Signal handlers (`sigchld_handler`, `sigint_handler`, `sigtstp_handler`) were implemented to handle job control signals, ensuring proper signal forwarding and preventing race conditions. Job control was achieved by managing a job list and using process IDs (PIDs) and job IDs (JIDs). The shell also supports pipelines by creating child processes connected via pipes.

## Skills and Knowledge Gained
Through this project, I gained a comprehensive understanding of Unix process control, signal handling, and shell programming. Key skills acquired include manipulating file descriptors for input/output redirection, using `fork` and `execve` for process creation, and handling Unix signals for job control. Testing was performed using provided trace files and automated scripts, ensuring the shell's functionality matched the reference solution. This project provided valuable insights and practical experience in systems programming, enhancing my ability to develop and debug complex software systems.
