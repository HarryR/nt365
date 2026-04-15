#include "y2.h"

void
cpycode( void )
   {
   /* copies code between \{ and \} */

   int c;

   c = unix_getc(finput);
   if( c == '\n' ) 
      {
      c = unix_getc(finput);
      lineno++;
      }

   writeline(ftable);

   while( c>=0 )
      {
      if( c=='\\' )
         if( (c=unix_getc(finput)) == '}' ) return;
         else putc('\\', ftable );
      if( c=='%' )
         if( (c=unix_getc(finput)) == '}' ) return;
         else putc('%', ftable );
      putc( c , ftable );
      if( c == '\n' ) ++lineno;
      c = unix_getc(finput);
      }
   error("eof before %%}" );
   }

/* MicroNT: #line emission is opt-in. midlpg can't parse #line directives,
   and originally MAKEFILE.INC stripped them with `qgrep -v "^#.*line"`
   before piping the .c into midlpg. We own the yacc source, so the -L
   command-line flag gates emission at the source instead. Default: off. */
int emit_line_directives = 0;

void writeline(FILE *fh) {
   char *psz;

   if (!emit_line_directives) return;

   fprintf( fh, "\n#line %d \"", lineno );
   psz = infile;
   while (*psz) {
      putc(*psz, fh);
      if (*psz == '\\') {
        putc('\\', fh);
      }
      psz++;
   }
   fprintf(fh, "\"\n");
}
