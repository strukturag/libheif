AC_DEFUN([AC_C_CXX_COMPILE_FLAGS],[
NEW_CFLAGS="$CFLAGS"
for ac_flag in $1
do
 AC_MSG_CHECKING(whether compiler supports $ac_flag)
 CFLAGS="$NEW_CFLAGS $ac_flag"
 AC_TRY_COMPILE(,[
  void f() {};
 ],[
  NEW_CFLAGS="$CFLAGS"
  AC_MSG_RESULT(yes)
 ],AC_MSG_RESULT(no))
done
CFLAGS="$CFLAGS $NEW_CFLAGS"
CXXFLAGS="$CXXFLAGS $NEW_CFLAGS"
])
