%{
#include <string.h>
#include "conf.tab.h"
#include "grok_config.h"
#include "stringhelper.h"

#define _GNU_SOURCE /* for strndup */
%}

%option noyywrap bison-bridge

true true|yes|on|1
false false|no|off|0
number [0-9]+

%x LEX_COMMENT
%x LEX_STRING

%%

program { return PROGRAM; }
load-patterns { return PROG_LOADPATTERNS; }

file { return PROG_FILE; }
follow { return FILE_FOLLOW; }

exec { return PROG_EXEC; }
restart-on-failure { return EXEC_RESTARTONFAIL; }
minimum-restart-delay { return EXEC_MINRESTARTDELAY; }
run-interval { return EXEC_RUNINTERVAL; }
read-stderr { return EXEC_READSTDERR; }

match { return PROG_MATCH; }
no-match { return PROG_NOMATCH; }
pattern { return MATCH_PATTERN; }
reaction { return MATCH_REACTION; }
shell { return MATCH_SHELL; }
flush { return MATCH_FLUSH; }

debug { return CONF_DEBUG; }

{true} { yylval->num = 1; return INTEGER; }
{false} { yylval->num = 0; return INTEGER; }
{number} { yylval->num = atoi(yytext); return INTEGER; }

"#" BEGIN(LEX_COMMENT);
<LEX_COMMENT>[^\n]* /* ignore comments */ //{ printf("Comment: %s\n", yytext); }
<LEX_COMMENT>\n   { yylineno++; BEGIN(INITIAL); } /* end comment */

\" { BEGIN(LEX_STRING); }
<LEX_STRING>((\\.)+|[^\\\"]+)* { 
 yylval->str = malloc(yyleng + 1);
 strcpy(yylval->str, yytext);
 yylval->str[yyleng] = '\0';
 //printf("String: '%s'\n", yylval->str);
 return QUOTEDSTRING;
}
<LEX_STRING>\" { BEGIN(INITIAL); }


\{ { return '{'; }
\} { return '}'; }
: { return ':'; }


[ \t] { /* ignore whitespace */ }
[\n] { yylineno++; }
%%
