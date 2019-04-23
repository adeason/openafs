dnl This checks if recvmmsg() is available (note the extra 'm'), along with the
dnl MSG_WAITFORONE flag
AC_DEFUN([OPENAFS_FUNC_RECVMMSG],
 [AS_CASE([$AFS_SYSNAME],
   [sun4x_511|sunx86_511],
     [AS_CASE([`uname -v`],
       [11.3|11.3.*],
         dnl A recvmmsg is available in an update to Solaris 11.3, but
         dnl building with it and then running that binary on later Solaris
         dnl releases causes the recvmmsg to always return errors (due to
         dnl something involving changes in the defaults for XPG/XOPEN
         dnl compilation flags). So to keep our lives a little simpler, just
         dnl pretend Solaris 11.3 doesn't have recvmmsg at all.
         [AC_MSG_WARN([Some releases of Solaris 11.3 result in building with a buggy recvmmsg. Forcibly disabling recvmmsg support.])
          ac_cv_openafs_func_recvmmsg=no])])

  AC_CACHE_CHECK([for recvmmsg(MSG_WAITFORONE)],
   [ac_cv_openafs_func_recvmmsg],

   [AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
      [[#include <sys/socket.h>]],
      [[int code;
        int sock = 0;
        struct mmsghdr msgvec;
        struct timespec timeo;
        code = recvmmsg(sock, &msgvec, 1, MSG_WAITFORONE, &timeo);]])],
    [ac_cv_openafs_func_recvmmsg=yes],
    [ac_cv_openafs_func_recvmmsg=no])])

  AS_IF([test x"$ac_cv_openafs_func_recvmmsg" = xyes],
   [AC_DEFINE([HAVE_RECVMMSG], [1],
     [define if recvmmsg() is available with MSG_WAITFORONE])])])
