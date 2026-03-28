/* Entry point for user programs – calls main then exits */
    .global _start
_start:
    call main
    pushl $0          /* exit status */
    call sys_exit