# tsh-mini-shell 🖥️
 University project making a replica mini shell command prompt using C

## Introduction
In this project, I developed a miniature Unix shell program called `tsh` (Tiny Shell) to gain a deeper understanding of process control, signals, and pipes. The shell supports running external programs, managing job states, handling input/output redirection, processing signals, and creating pipelines. Key functionalities include parsing and interpreting commands, recognizing built-in commands like `quit`, `fg`, `bg`, and `jobs`, and handling signals such as SIGINT (Ctrl-C) and SIGTSTP (Ctrl-Z).

## Design and Implementation
The `tsh` shell was built using a structured approach where core functions were implemented to manage process creation, job control, and signal handling. The `eval` function parses the command line, determines whether the command is built-in or external, handles input/output redirection, and manages background and foreground jobs. For built-in commands, the `builtin_cmd` function handles immediate execution, while `do_bgfg` manages the `bg` and `fg` commands, altering job states and ensuring proper job control.

Signal handling was a critical aspect of the implementation. The `sigchld_handler` function catches SIGCHLD signals, ensuring terminated or stopped child processes are properly reaped to avoid zombie processes. The `sigint_handler` and `sigtstp_handler` functions manage SIGINT and SIGTSTP signals respectively, forwarding these signals to the entire foreground process group to handle job interruptions correctly. Additionally, job control was achieved by maintaining a job list, assigning both process IDs (PIDs) and job IDs (JIDs) for efficient job management. The `waitfg` function ensures the shell waits for foreground jobs to complete before proceeding.

![image](https://github.com/user-attachments/assets/b6acbd13-a217-46d6-bc10-205f695bab41)

## Skills and Knowledge Gained
Through this project, I gained a comprehensive understanding of Unix process control, signal handling, and shell programming. Key skills acquired include manipulating file descriptors for input/output redirection, using `fork` and `execve` for process creation, and handling Unix signals for job control. I learned to block and unblock signals using `sigprocmask` to prevent race conditions during process creation and signal handling. Implementing pipelines required understanding and using Unix pipes to connect multiple child processes.

Testing was performed using provided trace files and automated scripts, ensuring the shell's functionality matched the reference solution. The `sdriver.pl` program was instrumental in validating the shell's behavior against various scenarios, from simple command execution to complex job control tasks. This project provided valuable insights and practical experience in systems programming, enhancing my ability to develop and debug complex software systems, making me more adept at handling real-world programming challenges.

![Uploading image.png…]()

