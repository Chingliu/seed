//// seed.c ///////////////////////////////////////////////////////////////////
// seed is a lua executable which loads data and lua code embedded in the
// executable. Specifically, it loads from a zip file concatenated to its
// executable. Initially /init.lua from the zip file is run, and a loader is
// added to package.loaders so that 'require' searches inside the zip file. If
// a module is not found in the archive, then the default loaders are used.
//
// In addition to using 'require', lua files can be loaded with seed.loadfile,
// which is like loadfile except that it gets the file from inside the archive.
// Basic reading of arbitrary files from the archive is supported.
// First use seed.open, which is like io.open except that only the "rb" option
// is accepted. The returned file object implements only :read, which is like
// file:read from io but only supporting arguments of either an integer or the
// string '*a'. :close works as normal, but does not need to be called when you
// can wait for the garbage collector to do it for you.
//
// Binary (i.e. native shared library) modules cannot be loaded from the
// archive due to OS limitations, but there are two ways to load them. They can
// be loaded normally as shared libraries by 'require' according to
// package.cpath, but your executable won't be self-contained anymore. The
// other option is to statically link them into the executable, and register
// them in package.preloaders by adding them to the init_preloaders function
// below.

//// legal info ///////////////////////////////////////////////////////////////
// Copyright (c) 2009 Henk Boom
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

//// headers and stuff ////////////////////////////////////////////////////////

#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <physfs.h>

//// preloaders ///////////////////////////////////////////////////////////////
// to statically link in binary lua modules:
//
// - use the right linker options (or just include the .a in the link line)
// - add them to init_preloaders right below
//
// You don't need to manually declare them or include anything, the
// REGISTER_LOADER macro handles that.

#define REGISTER_LOADER(module_name, loader) \
    int loader(lua_State *L); \
    lua_pushcfunction(L, loader); \
    lua_setfield(L, -2, module_name)

void init_preloaders(lua_State *L)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");

    // add your custom loaders here, they look like this:
    REGISTER_LOADER("seed", luaopen_seed);
    
    lua_pop(L, 2);
}

//// seed module //////////////////////////////////////////////////////////////

struct reader_data
{
    char buffer[LUAL_BUFFERSIZE];
    PHYSFS_file * file;
};

static const char * reader(lua_State *L, void * data, size_t *size)
{
    struct reader_data * rd = (struct reader_data *)data;
    PHYSFS_sint64 ret = PHYSFS_read(rd->file, rd->buffer, 1, BUFSIZ);
    if(ret == -1)
        luaL_error(L, "error reading file: %s", PHYSFS_getLastError());
    *size = (size_t) ret;
    return rd->buffer;
}

static int seed_loadfile(lua_State *L)
// string -> chunk
{
    struct reader_data rd;
    const char * filename = luaL_checkstring(L, 1);
    rd.file = PHYSFS_openRead(filename);
    if(rd.file == NULL)
        return luaL_error(L, "couldn't open file: '%s'", filename);
    if(lua_load(L, reader, &rd, filename))
        return lua_error(L);
    PHYSFS_close(rd.file);
    return 1;
}

static PHYSFS_file ** check_seed_file(lua_State *L, int index)
{
    return (PHYSFS_file **)luaL_checkudata(L, index, "seed.file");
}

static PHYSFS_file ** check_seed_open_file(lua_State *L, int index)
{
    PHYSFS_file ** file = check_seed_file(L, index);
    if(!*file) luaL_error(L, "attempt to use a closed file");
    return file;
}

static int seed_open(lua_State *L)
// filename, mode -> physfs file
{
    static const char * modes[] = { "rb", /*"wb", "ab",*/ NULL };
    enum { READ /*, WRITE, APPEND*/ };

    const char * filename = luaL_checkstring(L, 1);
    int mode = luaL_checkoption(L, 2, "rb", modes);

    PHYSFS_file * file;

    if(mode == READ)
        file = PHYSFS_openRead(filename);
    //else if(mode == WRITE)
    //    file = PHYSFS_openWrite(filename);
    //else if(mode == APPEND)
    //    file = PHYSFS_openAppend(filename);
    else assert(0);

    if(file)
    {
        *(PHYSFS_file **)lua_newuserdata(L, sizeof(file)) = file;
        luaL_getmetatable(L, "seed.file");
        lua_setmetatable(L, -2);
        return 1;
    }
    else
    {
        lua_pushnil(L);
        lua_pushstring(L, PHYSFS_getLastError());
        return 2;
    }
}

static int seed_file_close(lua_State *L)
{
    PHYSFS_file ** file = check_seed_open_file(L, 1);
    if(PHYSFS_close(*file))
    {
        *file = NULL;
        lua_pushboolean(L, 1);
        return 1;
    }
    else
    {
        lua_pushnil(L);
        lua_pushstring(L, PHYSFS_getLastError());
        return 2;
    }
}

static int read_bytes(lua_State *L, PHYSFS_file *file, PHYSFS_uint64 bytes)
{
    struct luaL_Buffer lbuf;
    luaL_buffinit(L, &lbuf);
    
    PHYSFS_sint64 read;
    do
    {
        PHYSFS_uint32 to_read = LUAL_BUFFERSIZE;
        if(to_read > bytes) to_read = bytes;
        char * buf = luaL_prepbuffer(&lbuf);

        read = PHYSFS_read(file, buf, 1, to_read);

        if(read > 0)
        {
            luaL_addsize(&lbuf, read);
            bytes = bytes - read;
        }
    } while(read > 0 && bytes > 0);

    luaL_pushresult(&lbuf);
    return 1;
}

static int seed_file_read(lua_State *L)
{
    PHYSFS_file ** file = check_seed_open_file(L, 1);

    if(lua_isnumber(L, 2))
    {
        int bytes = lua_tointeger(L, 2);
        luaL_argcheck(L, bytes >= 0, 2, "negative number of bytes");
        if(PHYSFS_eof(*file))
            lua_pushnil(L);
        else
            read_bytes(L, *file, bytes);
        return 1;
    }
    else
    {
        static const char * mode_strings[] = { "*a", NULL };
        enum modes { ALL };

        int mode = luaL_checkoption(L, 2, "*a", mode_strings);
        if(mode == ALL)
            return read_bytes(L, *file, (PHYSFS_uint64)-1);
        else assert(0);
    }
}

static int seed_file_gc(lua_State *L)
{
    PHYSFS_file ** file = check_seed_file(L, 1);
    if(*file) seed_file_close(L);
    return 0;
}

static const luaL_reg seed_file_lib[] =
{
    {"close", seed_file_close},
    {"read", seed_file_read},
    {"__gc", seed_file_gc},
    {NULL, NULL}
};

static const luaL_reg seed_lib[] =
{
    {"loadfile", seed_loadfile},
    {"open", seed_open},
    {NULL, NULL}
};

int luaopen_seed(lua_State *L)
{
    luaL_newmetatable(L, "seed.file");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_register(L, NULL, seed_file_lib);

    lua_newtable(L);
    luaL_register(L, NULL, seed_lib);
    return 1;
}

//// physfs require support ///////////////////////////////////////////////////

static void to_filename(lua_State *L, int index)
// converts the module name at the given index to a filename, and pushes it on
// the stack
{
    const char * module = luaL_checkstring(L, index);
    lua_pushstring(L, "/");
    luaL_gsub(L, module, ".", "/");
    lua_pushstring(L, ".lua");
    lua_concat(L, 3);
}

static int physfs_searcher(lua_State *L)
// module name -> nil or loader function
{
    to_filename(L, 1);
    const char * filename = lua_tostring(L, 2);
    if(PHYSFS_exists(filename))
    {
        // call physfs_loadfile with filename as argument
        lua_pushcfunction(L, seed_loadfile);
        lua_insert(L, 2);
        lua_call(L, 1, 1);
    }
    else
    {
        lua_pushfstring(L, "\n\tno physfs file '%s'", filename);
    }
    return 1;
}

static void init_physfs_loader(lua_State *L)
{
    // get table.insert
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "insert");
    lua_remove(L, -2);

    // get package.loaders
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaders");
    lua_remove(L, -2);

    // insert physfs loader
    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);
    lua_pushnumber(L, 2);
    lua_pushcfunction(L, physfs_searcher);
    lua_call(L, 3, 0);

    lua_pop(L, 2);
}

//// main program /////////////////////////////////////////////////////////////

const char * basename(const char * filename)
{
    const char * last_found = filename;
    const char * sep;
    do
    {
        sep = strstr(last_found, PHYSFS_getDirSeparator());
        last_found = sep ? sep + 1 : last_found;
    } while (sep);
    return last_found;
}

int main(int argc, char ** argv)
{
    // number of arguments to skip before the script's real arguments
    int skip_arg = 1;

    // init physfs
    if(!PHYSFS_init(argv[0]))
    {
        fprintf(stderr, "physfs init failed: %s", PHYSFS_getLastError());
        return 1;
    }

    // get executable path
    const char *directory = PHYSFS_getBaseDir();
    const char *executable = basename(argv[0]);
    char *path = malloc(strlen(directory) + strlen(executable) + 1);
    strcpy(path, directory);
    strcat(path, executable);
    // try to mount the executable as an archive, on failure try to mount the
    // first argument instead
    if(!PHYSFS_mount(path, "/", 0))
    {
        skip_arg = skip_arg + 1;
        if(argc < 2 || !PHYSFS_mount(argv[1], "/", 0))
        {
            fprintf(stderr, "no archive found in the executable nor in the "
                    "first argument\n");
            return 1;
        }
    }
    free(path);

    // load lua and libraries
    lua_State *L = lua_open();
    luaL_openlibs(L);
    init_physfs_loader(L);
    init_preloaders(L);
    
    // load arguments (and pre-arguments) into a global 'arg' table
    lua_newtable(L);
    for(int i = 0; i < argc; i++)
    {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - skip_arg + 1);
    }
    lua_setglobal(L, "arg");

    // open app, with error reporting
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");

    lua_pushcfunction(L, seed_loadfile);
    lua_pushstring(L, "init.lua");
    int error = lua_pcall(L, 1, 1, 0); // load file

    if(!error)
    {
        // load command-line arguments as function arguments
        lua_checkstack(L, argc - skip_arg);
        for(int i = 1; i <= argc - skip_arg; i++)
            lua_pushstring(L, argv[i + skip_arg - 1]);
        error = lua_pcall(L, argc - skip_arg, 0, -2);  // run the result
    }

    if(error)
        fprintf(stderr, "%s\n", lua_tostring(L, -1));

    lua_close(L);
    PHYSFS_deinit();
    return error;
}

