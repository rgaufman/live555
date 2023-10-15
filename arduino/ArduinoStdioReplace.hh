/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/

#pragma once

/**
 * @brief For Arduino we replace the stdio calls with the actual AbstractFile calls
 * 
 */
#ifndef DEACTIVATE_STDIO_ABSTRACTION
#define fopen(a,b) (FILE*) (IO555->fopen(a,b))
#define fclose IO555->fclose
#define fgetc IO555->fgetc
#define fputc IO555->fputc
#define fseek IO555->fseek
#define ftell IO555->ftell
#define fread IO555->fread
#define fwrite IO555->fwrite
#define ferror IO555->ferror
#define ungetc IO555->ungetc
#define feof(f) IO555->feof(f)
#define rewind(f) IO555->rewind(f)
#define fflush IO555->fflush
#define clearerr IO555->clearerr
#endif