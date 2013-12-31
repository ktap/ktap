" Vim syntax file
" Language:     ktap
" Maintainer:	Jovi Zhangwei <jovi.zhangwei@gmail.com>
" First Author:	Jovi Zhangwei <jovi.zhangwei@gmail.com>
" Last Change:	2013 Dec 19

" For version 5.x: Clear all syntax items
" For version 6.x: Quit when a syntax file was already loaded
if version < 600
  syn clear
elseif exists("b:current_syntax")
  finish
endif

setlocal iskeyword=@,48-57,_,$

syn keyword ktapStatement contained break continue return containedin=ktapBlock
syn keyword ktapRepeat contained while for in containedin=ktapBlock
syn keyword ktapConditional contained if else elseif containedin=ktapBlock
syn keyword ktapDeclaration trace trace_end function var

syn region ktapTraceDec start="\<trace\>"lc=5 end="{"me=s-1 contains=ktapString,ktapNumber
syn region ktapTraceDec start="\<trace_end\>"lc=9 end="{"me=s-1 contains=ktapString,ktapNumber
syn match ktapTrace contained "\<\w\+\>" containedin=ktapTraceDec

syn region ktapFuncDec start="\<function\>"lc=8 end=":\|("me=s-1 contains=ktapString,ktapNumber
syn match ktapFuncCall contained "\<\w\+\ze\(\s\|\n\)*(" containedin=ktapBlock
syn match ktapFunc contained "\<\w\+\>" containedin=ktapFuncDec,ktapFuncCall

syn match ktapStat contained "@\<\w\+\ze\(\s\|\n\)*(" containedin=ktapBlock

" decimal number
syn match ktapNumber "\<\d\+\>" containedin=ktapBlock
" octal number
syn match ktapNumber "\<0\o\+\>" contains=ktapOctalZero containedin=ktapBlock
" Flag the first zero of an octal number as something special
syn match ktapOctalZero contained "\<0"
" flag an octal number with wrong digits
syn match ktapOctalError "\<0\o*[89]\d*" containedin=ktapBlock
" hex number
syn match ktapNumber "\<0x\x\+\>" containedin=ktapBlock
" numeric arguments
syn match ktapNumber "\<\$\d\+\>" containedin=ktapBlock
syn match ktapNumber "\<\$#" containedin=ktapBlock

syn region ktapString oneline start=+"+ skip=+\\"+ end=+"+ containedin=ktapBlock
" string arguments
syn match ktapString "@\d\+\>" containedin=ktapBlock
syn match ktapString "@#" containedin=ktapBlock
syn region ktapString2 matchgroup=ktapString start="\[\z(=*\)\[" end="\]\z1\]" contains=@Spell

syn region ktapBlock fold matchgroup=ktapBlockEnds start="{"rs=e end="}"re=s containedin=ktapBlock

" syn keyword ktapTodo contained TODO FIXME XXX

syn match ktapComment "#.*" containedin=ktapBlock

" treat ^#! as special
syn match ktapSharpBang "^#!.*"

syn keyword ktapArgs argevent argname arg1 arg2 arg3 arg4 arg5 arg6 arg7 arg8 arg9

syn keyword ktapFunc printf print
syn keyword ktapFunc gettimeofday_us
syn keyword ktapFunc pairs

syn match ktapFunc /\<ffi\.cdef\>/
syn match ktapFunc /\<ffi\.new\>/
syn match ktapFunc /\<ffi\.free\>/
syn match ktapFunc /\<ffi\.C\>/



" Define the default highlighting.
" For version 5.7 and earlier: only when not done already
" For version 5.8 and later: only when an item doesn't have highlighting yet
if version >= 508 || !exists("did_lua_syntax_inits")
  if version < 508
    let did_lua_syntax_inits = 1
    command -nargs=+ HiLink hi link <args>
  else
    command -nargs=+ HiLink hi def link <args>
  endif

  HiLink ktapNumber		Number
  HiLink ktapOctalZero		PreProc " c.vim does it this way...
  HiLink ktapOctalError		Error
  HiLink ktapString		String
  HiLink ktapString2		String
  HiLink ktapTodo		Todo
  HiLink ktapComment		Comment
  HiLink ktapSharpBang		PreProc
  HiLink ktapStatement		Statement
  HiLink ktapConditional	Conditional
  HiLink ktapRepeat		Repeat
  HiLink ktapTrace		Function
  HiLink ktapFunc		Function
  HiLink ktapStat		Function
  HiLink ktapDeclaration	Typedef
  HiLink ktapArgs		Identifier

  delcommand HiLink
endif

let b:current_syntax = "ktap"
