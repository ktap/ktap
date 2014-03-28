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

syn keyword ktapStatement break continue return
syn keyword ktapRepeat while for in
syn keyword ktapConditional if else elseif
syn keyword ktapDeclaration trace trace_end
syn keyword ktapIdentifier var
syn keyword ktapFunction function
syn match   ktapBraces "[{}\[\]]"
syn match   ktapParens "[()]"
syn keyword ktapReserved argstr probename arg0 arg1 arg2 arg3 arg4 arg5 arg6 arg7 arg8 arg9
syn keyword ktapReserved cpu pid tid uid execname


syn region ktapTraceDec start="\<trace\>"lc=5 end="{"me=s-1 contains=ktapString,ktapNumber
syn region ktapTraceDec start="\<trace_end\>"lc=9 end="{"me=s-1 contains=ktapString,ktapNumber
syn match ktapTrace contained "\<\w\+\>" containedin=ktapTraceDec

syn region ktapFuncDec start="\<function\>"lc=8 end=":\|("me=s-1 contains=ktapString,ktapNumber
syn match ktapFuncCall contained "\<\w\+\ze\(\s\|\n\)*("
syn match ktapFunc contained "\<\w\+\>" containedin=ktapFuncDec,ktapFuncCall

syn match ktapStat contained "@\<\w\+\ze\(\s\|\n\)*("

" decimal number
syn match ktapNumber "\<\d\+\>"
" octal number
syn match ktapNumber "\<0\o\+\>" contains=ktapOctalZero
" Flag the first zero of an octal number as something special
syn match ktapOctalZero contained "\<0"
" flag an octal number with wrong digits
syn match ktapOctalError "\<0\o*[89]\d*"
" hex number
syn match ktapNumber "\<0x\x\+\>"
" numeric arguments
syn match ktapNumber "\<\$\d\+\>"
syn match ktapNumber "\<\$#"

syn region ktapString oneline start=+"+ skip=+\\"+ end=+"+ 
" string arguments
syn match ktapString "@\d\+\>"
syn match ktapString "@#"
syn region ktapString2 matchgroup=ktapString start="\[\z(=*\)\[" end="\]\z1\]" contains=@Spell

" syn keyword ktapTodo contained TODO FIXME XXX

syn match ktapComment "#.*"

" treat ^#! as special
syn match ktapSharpBang "^#!.*"


syn keyword ktapFunc printf print print_hist stack
syn keyword ktapFunc gettimeofday_us
syn keyword ktapFunc pairs


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
  HiLink ktapFunction		Function
  HiLink ktapBraces		Function
  HiLink ktapDeclaration	Typedef
  HiLink ktapIdentifier		Identifier
  HiLink ktapReserved		Keyword

  delcommand HiLink
endif

let b:current_syntax = "ktap"
