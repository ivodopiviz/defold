#include <gtest/gtest.h>

#include "script.h"

#include <dlib/dstrings.h>
#include <dlib/hash.h>
#include <dlib/log.h>
#include <dlib/configfile.h>

#include <string.h>

extern "C"
{
#include <lua/lauxlib.h>
#include <lua/lualib.h>
}

#define PATH_FORMAT "build/default/src/test/%s"

class ScriptTest : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        dmSetCustomLogCallback(LogCallback, this);
        m_Context = dmScript::NewContext(0x0, 0, true);
        dmScript::Initialize(m_Context);
        L = dmScript::GetLuaState(m_Context);
    }

    virtual void TearDown()
    {
        dmScript::Finalize(m_Context);
        dmScript::DeleteContext(m_Context);
        dmSetCustomLogCallback(0x0, 0x0);
    }

    const char* RemoveTableAddresses(char* str)
    {
        char* read_ptr = str;
        char* write_ptr = str;
        const char* address_prefix = " --[[0";
        size_t address_prefix_length = strlen(address_prefix);
        char* addr_ptr = strstr(str, address_prefix);
        while (addr_ptr != 0x0)
        {
            uintptr_t offset = addr_ptr - read_ptr;
            size_t copy_length = offset + address_prefix_length;
            while (copy_length --)
            {
                *write_ptr++ = *read_ptr++;
            }
            char* address_end = strstr(read_ptr, "]]");
            if (address_end && (address_end - read_ptr) <= 18)
            {
                // Only skip if it was actually short enough to be an address
                read_ptr = address_end;
            }
            addr_ptr = strstr(read_ptr, address_prefix);
        }
        while (*read_ptr)
        {
            *write_ptr++ = *read_ptr++;
        }
        *write_ptr = 0;
        return str;
    }

    char* GetLog()
    {
        m_Log.Push('\0');
        return m_Log.Begin();
    }

    void AppendToLog(const char* log)
    {
        uint32_t len = strlen(log);
        m_Log.SetCapacity(m_Log.Size() + len + 1);
        m_Log.PushArray(log, len);
    }

    static void LogCallback(void* user_data, const char* log)
    {
        ScriptTest* i = (ScriptTest*)user_data;
        i->AppendToLog(log);
    }

    dmScript::HContext m_Context;
    lua_State* L;
    dmArray<char> m_Log;
};

bool RunFile(lua_State* L, const char* filename)
{
    char path[64];
    DM_SNPRINTF(path, 64, PATH_FORMAT, filename);
    if (luaL_dofile(L, path) != 0)
    {
        dmLogError("%s", lua_tolstring(L, -1, 0));
        return false;
    }
    return true;
}

bool RunString(lua_State* L, const char* script)
{
    if (luaL_dostring(L, script) != 0)
    {
        dmLogError("%s", lua_tolstring(L, -1, 0));
        return false;
    }
    return true;
}

TEST_F(ScriptTest, TestPrint)
{
    int top = lua_gettop(L);
    ASSERT_TRUE(RunString(L, "print(\"test print\")"));
    ASSERT_TRUE(RunString(L, "print(\"test\", \"multiple\")"));

    ASSERT_EQ(top, lua_gettop(L));
    ASSERT_STREQ("DEBUG:SCRIPT: test print\nDEBUG:SCRIPT: test\tmultiple\n", GetLog());
}

TEST_F(ScriptTest, TestPPrint)
{
    int top = lua_gettop(L);
    ASSERT_TRUE(RunFile(L, "test_script.luac"));
    ASSERT_EQ(top, lua_gettop(L));

    const char* result = RemoveTableAddresses(GetLog());
    ASSERT_EQ(result, strstr(result, "DEBUG:SCRIPT: testing pprint\n"));
    ASSERT_NE((const char*)0, strstr(result, "{ --[[0]]"));
    ASSERT_NE((const char*)0, strstr(result, "      m = vmath.matrix4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)"));
    ASSERT_NE((const char*)0, strstr(result, "  3 = { } --[[0]]"));
    ASSERT_NE((const char*)0, strstr(result, "  2 = \"hello\""));
    ASSERT_NE((const char*)0, strstr(result, "      m = vmath.matrix4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1),"));
    ASSERT_NE((const char*)0, strstr(result, "      q = vmath.quat(0, 0, 0, 1),"));
    ASSERT_NE((const char*)0, strstr(result, "      n = vmath.vector3(0, 0, 0)"));
    ASSERT_NE((const char*)0, strstr(result, "DEBUG:SCRIPT: 5"));
}


TEST_F(ScriptTest, TestCircularRefPPrint)
{
    int top = lua_gettop(L);
    ASSERT_TRUE(RunFile(L, "test_circular_ref_pprint.luac"));
    ASSERT_EQ(top, lua_gettop(L));

    const char* result = RemoveTableAddresses(GetLog());
    ASSERT_EQ(result, strstr(result, "DEBUG:SCRIPT: testing pprint with circular ref\n"));
    ASSERT_NE((const char*)0, strstr(result, "DEBUG:SCRIPT: \n"));
    ASSERT_NE((const char*)0, strstr(result, "{ --[[0]]"));
    ASSERT_NE((const char*)0, strstr(result, "  foo = \"an old man was telling stories of circular references.\""));
    ASSERT_NE((const char*)0, strstr(result, "  gnu = { --[[0]]"));
    ASSERT_NE((const char*)0, strstr(result, "    gnat = { ... } --[[0]]"));
    ASSERT_NE((const char*)0, strstr(result, "    bar = \"It was a dark and stormy night,\""));
    ASSERT_NE((const char*)0, strstr(result, "  }"));
    ASSERT_NE((const char*)0, strstr(result, "}"));
}

TEST_F(ScriptTest, TestPPrintTruncate)
{
    int top = lua_gettop(L);
    ASSERT_TRUE(RunFile(L, "test_pprint_truncate.luac"));
    ASSERT_EQ(top, lua_gettop(L));
    const char* log = GetLog();
    const char* truncate_message_addr = strstr(log, "...\n[Output truncated]\n");
    ASSERT_NE((const char*)0x0, truncate_message_addr);
    truncate_message_addr += 23;
    ASSERT_EQ(0, *truncate_message_addr);
}

TEST_F(ScriptTest, TestRandom)
{
    int top = lua_gettop(L);
    ASSERT_TRUE(RunString(L, "math.randomseed(123)"));
    ASSERT_TRUE(RunString(L, "assert(math.random(0,100) == 1)"));
    ASSERT_TRUE(RunString(L, "assert(math.random(0,100) == 58)"));
    ASSERT_TRUE(RunString(L, "assert(math.abs(math.random() - 0.70419311523438) < 0.0000001)"));
    ASSERT_EQ(top, lua_gettop(L));
}

static int LuaCallCallback(lua_State* L)
{
    luaL_checktype(L, -1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_setglobal(L, "_callback");
    lua_pushlightuserdata(L, L);
    lua_setglobal(L, "_state");
    return 0;
}

/*
 * This test mimicks how callbacks are handled in extensions, e.g. facebook, iap, push, with respect to coroutine safety
 */
TEST_F(ScriptTest, TestCoroutineCallback)
{
    int top = lua_gettop(L);
    // Setup test function
    lua_pushcfunction(L, LuaCallCallback);
    lua_setglobal(L, "call_callback");
    // Run script
    ASSERT_TRUE(RunFile(L, "test_script_coroutine.luac"));
    // Retrieve lua state from c func
    lua_getglobal(L, "_state");
    lua_State* state = (lua_State*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    // Retrieve and run callback from c func
    lua_getglobal(L, "_callback");
    luaL_checktype(L, -1, LUA_TFUNCTION);
    int ret = dmScript::PCall(dmScript::GetMainThread(state), 0, LUA_MULTRET);
    ASSERT_EQ(0, ret);

    // Retrieve coroutine
    lua_getglobal(L, "global_co");
    int status = lua_status(lua_tothread(L, -1));
    lua_pop(L, 1);
    ASSERT_NE(LUA_YIELD, status);

    ASSERT_EQ(top, lua_gettop(L));
}

struct TestDummy {
    uint32_t        m_Dummy;
    dmMessage::URL  m_URL;
    int             m_InstanceReference;
    int             m_ContextTableReference;
};

static int TestGetURL(lua_State* L) {
    TestDummy* dummy = (TestDummy*)lua_touserdata(L, 1);
    dmScript::PushURL(L, dummy->m_URL);
    return 1;
}

static int TestGetUserData(lua_State* L) {
    TestDummy* dummy = (TestDummy*)lua_touserdata(L, 1);
    lua_pushlightuserdata(L, (void*)(uintptr_t)dummy->m_Dummy);
    return 1;
}

static int TestResolvePath(lua_State* L) {
    dmScript::PushHash(L, dmHashString64(luaL_checkstring(L, 2)));
    return 1;
}

static int TestIsValid(lua_State* L)
{
    TestDummy* dummy = (TestDummy*)lua_touserdata(L, 1);
    lua_pushboolean(L, dummy != 0x0 && dummy->m_Dummy != 0);
    return 1;
}

static int TestGetContextTableRef(lua_State* L)
{
    const int self_index = 1;

    int top = lua_gettop(L);

    TestDummy* i = (TestDummy*)lua_touserdata(L, self_index);
    lua_pushnumber(L, i ? i->m_ContextTableReference : LUA_NOREF);

    assert(top + 1 == lua_gettop(L));

    return 1;
}

static int TestToString(lua_State* L)
{
    lua_pushstring(L, "TestDummy");
    return 1;
}

static const luaL_reg Test_methods[] =
{
    {0,0}
};

static const luaL_reg Test_meta[] =
{
    {dmScript::META_TABLE_GET_URL,                  TestGetURL},
    {dmScript::META_TABLE_GET_USER_DATA,            TestGetUserData},
    {dmScript::META_TABLE_RESOLVE_PATH,             TestResolvePath},
    {dmScript::META_TABLE_IS_VALID,                 TestIsValid},
    {dmScript::META_GET_INSTANCE_CONTEXT_TABLE_REF, TestGetContextTableRef},
    {"__tostring",                                  TestToString},
    {0, 0}
};

#define ASSERT_EQ_URL(exp, act)\
    ASSERT_EQ(exp.m_Socket, act.m_Socket);\
    ASSERT_EQ(exp.m_Path, act.m_Path);\
    ASSERT_EQ(exp.m_Fragment, act.m_Fragment);\

#define ASSERT_NE_URL(exp, act)\
    ASSERT_NE(exp.m_Socket, act.m_Socket);\
    ASSERT_NE(exp.m_Path, act.m_Path);\
    ASSERT_NE(exp.m_Fragment, act.m_Fragment);\

TEST_F(ScriptTest, TestUserType) {
    const char* type_name = "TestType";
    dmScript::RegisterUserType(L, type_name, Test_methods, Test_meta);

    // Create an instance
    TestDummy* dummy = (TestDummy*)lua_newuserdata(L, sizeof(TestDummy));
    dummy->m_Dummy = 1;
    // dummy URL value
    memset(&dummy->m_URL, 0xABCD, sizeof(dmMessage::URL));
    luaL_getmetatable(L, type_name);
    lua_setmetatable(L, -2);
    dummy->m_InstanceReference = dmScript::Ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    dummy->m_ContextTableReference = dmScript::Ref(L, LUA_REGISTRYINDEX);

    // Invalid
    ASSERT_FALSE(dmScript::IsInstanceValid(L));

    // Set as current instance
    lua_rawgeti(L, LUA_REGISTRYINDEX, dummy->m_InstanceReference);
    dmScript::SetInstance(L);

    // Valid
    ASSERT_TRUE(dmScript::IsInstanceValid(L));

    // GetURL
    dmMessage::URL url;
    dmMessage::ResetURL(url);
    ASSERT_NE_URL(dummy->m_URL, url);
    dmScript::GetURL(L, &url);
    ASSERT_EQ_URL(dummy->m_URL, url);

    // GetUserData
    uintptr_t user_data;
    ASSERT_FALSE(dmScript::GetUserData(L, &user_data, "incorrect_type"));
    ASSERT_TRUE(dmScript::GetUserData(L, &user_data, type_name));
    ASSERT_EQ(dummy->m_Dummy, user_data);

    // ResolvePath
    dmMessage::ResetURL(url);
    dmMessage::Result result = dmScript::ResolveURL(L, "test_path", &url, &dummy->m_URL);
    ASSERT_EQ(dmMessage::RESULT_OK, result);
    ASSERT_EQ(dmHashString64("test_path"), url.m_Path);

    dmMessage::HSocket socket = 0;
    result = dmMessage::NewSocket("default", &socket);
    ASSERT_EQ(dmMessage::RESULT_OK, result);

    dmMessage::ResetURL(url);
    result = dmScript::ResolveURL(L, "default:/foo#bar", &url, &dummy->m_URL);

    ASSERT_EQ(dmMessage::RESULT_OK, result);
    ASSERT_STREQ("default", dmMessage::GetSocketName(url.m_Socket));
    ASSERT_EQ(dmHashString64("/foo"), url.m_Path);
    ASSERT_EQ(dmHashString64("bar"), url.m_Fragment);

    result = dmMessage::DeleteSocket(socket);
    ASSERT_EQ(dmMessage::RESULT_OK, result);

    lua_pushstring(L, "__context_value_4711_");
    lua_pushnumber(L, 4711);
    ASSERT_TRUE(dmScript::SetInstanceContextValue(L));

    lua_pushstring(L, "__context_value_666_");
    lua_pushnumber(L, 666);
    ASSERT_TRUE(dmScript::SetInstanceContextValue(L));

    lua_pushstring(L, "__context_value_4711_");
    dmScript::GetInstanceContextValue(L);
    ASSERT_EQ(4711, lua_tonumber(L, -1));
    lua_pop(L, 1);

    lua_pushstring(L, "__context_value_666_");
    dmScript::GetInstanceContextValue(L);
    ASSERT_EQ(666, lua_tonumber(L, -1));
    lua_pop(L, 1);

    lua_pushstring(L, "__context_value_invalid_");
    dmScript::GetInstanceContextValue(L);
    ASSERT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);

    dmScript::Unref(L, LUA_REGISTRYINDEX, dummy->m_ContextTableReference);
    dummy->m_ContextTableReference = LUA_NOREF;

    lua_pushstring(L, "__context_value_4711_");
    dmScript::GetInstanceContextValue(L);
    ASSERT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);

    // Destruction
    dmScript::Unref(L, LUA_REGISTRYINDEX, dummy->m_InstanceReference);
    memset(dummy, 0, sizeof(TestDummy));

    lua_pushnil(L);
    dmScript::SetInstance(L);
    lua_pushstring(L, "__context_value_4711_");
    dmScript::GetInstanceContextValue(L);
    ASSERT_TRUE(lua_isnil(L, -1));
    lua_pop(L, 1);
}

#undef ASSERT_EQ_URL
#undef ASSERT_NE_URL

static int FailingFunc(lua_State* L) {
    return luaL_error(L, "this function does not work");
}

TEST_F(ScriptTest, TestErrorHandler) {

    int top = lua_gettop(L);

    lua_pushcfunction(L, FailingFunc);
    int result = dmScript::PCall(L, 0, LUA_MULTRET);

    ASSERT_EQ(LUA_ERRRUN, result);
    ASSERT_EQ(top, lua_gettop(L));
}

TEST_F(ScriptTest, TestStackCheck) {

    DM_LUA_STACK_CHECK(L, 0);
    {
        DM_LUA_STACK_CHECK(L, 1);
        lua_pushnumber(L, 0);
    }
    lua_pop(L, 1);
}

static int TestStackCheckErrorFunc(lua_State* L) {
    DM_LUA_STACK_CHECK(L, 0);
    return DM_LUA_ERROR("this function does not work");
}

TEST_F(ScriptTest, TestStackCheckError) {
    lua_pushcfunction(L, TestStackCheckErrorFunc);
    int result = dmScript::PCall(L, 0, LUA_MULTRET);
    ASSERT_EQ(LUA_ERRRUN, result);
}

TEST_F(ScriptTest, TestErrorHandlerFunction)
{
    int top = lua_gettop(L);

    const char *install_working =
        "sys.set_error_handler(function(type, error, traceback)\n"
        "    _type = type\n"
        "    _error = error\n"
        "    _traceback = traceback\n"
        "    print(\"type is \" .. _type)\n"
        "end)\n";

    ASSERT_TRUE(RunString(L, install_working));

    lua_pushcfunction(L, FailingFunc);
    int result = dmScript::PCall(L, 0, LUA_MULTRET);
    ASSERT_EQ(LUA_ERRRUN, result);
    ASSERT_EQ(top, lua_gettop(L));

    ASSERT_TRUE(RunString(L, "assert(_type == \"lua\")"));
    ASSERT_TRUE(RunString(L, "assert(_error == \"this function does not work\")"));
    ASSERT_TRUE(RunString(L, "assert(string.len(_traceback) > 15)"));

    // test the path with failing error handler
    const char *install_failing =
        "sys.set_error_handler(function(type, error, traceback)\n"
        "   _G.error(\"Unable to handle error\")\n"
        "end)\n";

    ASSERT_TRUE(RunString(L, install_failing));

    lua_pushcfunction(L, FailingFunc);
    result = dmScript::PCall(L, 0, LUA_MULTRET);
    ASSERT_EQ(LUA_ERRRUN, result);
    ASSERT_EQ(top, lua_gettop(L));
}


struct CallbackArgs
{
    dmhash_t a;
    float b;
};

static void LuaCallbackCustomArgs(lua_State* L, void* user_args)
{
    CallbackArgs* args = (CallbackArgs*)user_args;
    dmScript::PushHash(L, args->a);
    lua_pushnumber(L, args->b);
}


static int CreateAndPushInstance(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    const char* type_name = "TestType";
    dmScript::RegisterUserType(L, type_name, Test_methods, Test_meta);

    // Create an instance
    TestDummy* dummy = (TestDummy*)lua_newuserdata(L, sizeof(TestDummy));
    dummy->m_Dummy = 1;
    luaL_getmetatable(L, type_name);
    lua_setmetatable(L, -2);
    int instanceref = dmScript::Ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    dummy->m_ContextTableReference = dmScript::Ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, instanceref);
    dmScript::SetInstance(L);
    return instanceref;
}

TEST_F(ScriptTest, LuaCallbackHelpers)
{
    const char *install_working =
        "_self = 1\n"
        "_a = 1\n"
        "_b = 1\n"
        "function test_callback(self, a, b)\n"
        "    _a = a\n"
        "    _self = self\n"
        "    _b = b\n"
        "end\n";

    ASSERT_TRUE(RunString(L, install_working));

    int instanceref = CreateAndPushInstance(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, instanceref);
    TestDummy* dummy = (TestDummy*)lua_touserdata(L, -1);
    dmScript::SetInstance(L);

    ASSERT_TRUE(dmScript::IsInstanceValid(L));

    lua_getglobal(L, "test_callback");
    luaL_checktype(L, -1, LUA_TFUNCTION);

    int top = lua_gettop(L);

    dmScript::LuaCallbackInfo* cbk = dmScript::CreateCallback(L, -1);
    ASSERT_TRUE(IsValidCallback(cbk));

    dmScript::InvokeCallback(cbk, 0, 0); // no custom arguments, means the callback gets called with nil args

    ASSERT_TRUE(RunString(L, "assert(tostring(_self) == \"TestDummy\")"));
    ASSERT_TRUE(RunString(L, "assert(_a == nil)"));
    ASSERT_TRUE(RunString(L, "assert(_b == nil)"));

    CallbackArgs args = { dmHashString64("hello"), 42.0f };
    dmScript::InvokeCallback(cbk, LuaCallbackCustomArgs, (void*)&args);

    ASSERT_TRUE(RunString(L, "assert(tostring(_self) == \"TestDummy\")"));
    ASSERT_TRUE(RunString(L, "assert(_a == hash(\"hello\"))"));
    ASSERT_TRUE(RunString(L, "assert(_b == 42)"));

    ASSERT_TRUE(IsValidCallback(cbk));
    dmScript::DeleteCallback(cbk);
    ASSERT_FALSE(IsValidCallback(cbk));

    char* test_string = (char*)lua_newuserdata(L, strlen("test_ref_data") + 1);
    strcpy(test_string, "test_ref_data");
    int string_ref = dmScript::RefInInstance(L);
    dmScript::ResolveInInstance(L, string_ref);
    const char* verify_string = (const char*)lua_touserdata(L, -1);
    ASSERT_STREQ(test_string, verify_string);
    lua_pop(L, 1);

    dmScript::UnrefInInstance(L, string_ref);
    dmScript::ResolveInInstance(L, string_ref);
    void* unset_ref = lua_touserdata(L, -1);
    ASSERT_EQ(0x0, unset_ref);
    lua_pop(L, 1);

    char* test_string_gcd = (char*)lua_newuserdata(L, strlen("test_ref_data") + 1);
    strcpy(test_string_gcd, "test_ref_data");
    int string_ref_gcd = dmScript::RefInInstance(L);

    dmScript::Unref(L, LUA_REGISTRYINDEX, dummy->m_ContextTableReference);
    dmScript::Unref(L, LUA_REGISTRYINDEX, instanceref);

    dmScript::ResolveInInstance(L, string_ref_gcd);
    void* unset_ref_2 = lua_touserdata(L, -1);
    ASSERT_EQ(0x0, unset_ref_2);
    lua_pop(L, 1);

    ASSERT_EQ(top, lua_gettop(L));
}

TEST_F(ScriptTest, GetTableValues)
{
    const char *install_lua =
        "test_table = {\n"
        "    a = 1,\n"
        "    b = 2,\n"
        "    c = \"3\",\n"
        "    d = function() return 4+4 end,\n"
        "    e = {5}\n"
        "}\n"
        "empty_table = {}\n";

    ASSERT_TRUE(RunString(L, install_lua));

    int instanceref = CreateAndPushInstance(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, instanceref);
    TestDummy* dummy = (TestDummy*)lua_touserdata(L, -1);
    dmScript::SetInstance(L);

    ASSERT_TRUE(dmScript::IsInstanceValid(L));

    lua_getglobal(L, "test_table");
    luaL_checktype(L, -1, LUA_TTABLE);
    int table_index = lua_gettop(L);

    // Check for integer values
    ASSERT_EQ(1, dmScript::GetTableIntValue(L, table_index, "a", 0));
    ASSERT_EQ(2, dmScript::GetTableIntValue(L, table_index, "b", 0));
    ASSERT_EQ(0, dmScript::GetTableIntValue(L, table_index, "c", 0));
    ASSERT_EQ(0, dmScript::GetTableIntValue(L, table_index, "d", 0));
    ASSERT_EQ(0, dmScript::GetTableIntValue(L, table_index, "e", 0));

    // Check for string values
    ASSERT_STREQ(NULL, dmScript::GetTableStringValue(L, table_index, "a", NULL));
    ASSERT_STREQ(NULL, dmScript::GetTableStringValue(L, table_index, "b", NULL));
    ASSERT_STREQ("3", dmScript::GetTableStringValue(L, table_index, "c", NULL));
    ASSERT_STREQ(NULL, dmScript::GetTableStringValue(L, table_index, "d", NULL));
    ASSERT_STREQ(NULL, dmScript::GetTableStringValue(L, table_index, "e", NULL));
    lua_pop(L, 1);

    // Test empty table
    lua_getglobal(L, "empty_table");
    luaL_checktype(L, -1, LUA_TTABLE);
    table_index = lua_gettop(L);

    ASSERT_EQ(0, dmScript::GetTableIntValue(L, table_index, "a", 0));
    ASSERT_STREQ(NULL, dmScript::GetTableStringValue(L, table_index, "a", NULL));
    lua_pop(L, 1);

    dmScript::Unref(L, LUA_REGISTRYINDEX, dummy->m_ContextTableReference);
    dmScript::Unref(L, LUA_REGISTRYINDEX, instanceref);
}

struct TestScriptExtension
{
    int m_SelfRef;
    float m_DeltaT;
    static uint32_t m_InitializeCalled;
    static uint32_t m_UpdateCalled;
    static uint32_t m_FinalizedCalled;
    static uint32_t m_NewScriptWorldCalled;
    static uint32_t m_DeleteScriptWorldCalled;
    static uint32_t m_UpdateScriptWorldCalled;
    static uint32_t m_InitializeScriptInstancedCalled;
    static uint32_t m_FinalizeScriptInstancedCalled;
};

uint32_t TestScriptExtension::m_InitializeCalled = 0;
uint32_t TestScriptExtension::m_UpdateCalled = 0;
uint32_t TestScriptExtension::m_FinalizedCalled = 0;
uint32_t TestScriptExtension::m_NewScriptWorldCalled = 0;
uint32_t TestScriptExtension::m_DeleteScriptWorldCalled = 0;
uint32_t TestScriptExtension::m_UpdateScriptWorldCalled = 0;
uint32_t TestScriptExtension::m_InitializeScriptInstancedCalled = 0;
uint32_t TestScriptExtension::m_FinalizeScriptInstancedCalled = 0;

static TestScriptExtension* GetTestScriptExtension(dmScript::HContext context)
{
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);

    lua_pushstring(L, "__TestScriptExtension__");
    dmScript::GetContextValue(context);
    int ref = lua_tonumber(L, 1);
    lua_pop(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    TestScriptExtension* extension = (TestScriptExtension*)(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return extension;
}

void TestScriptExtensionInitialize(dmScript::HContext context)
{
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);

    TestScriptExtension* extension = (TestScriptExtension*)lua_newuserdata(L, sizeof(TestScriptExtension));
    memset(extension, 0, sizeof(TestScriptExtension));
    extension->m_SelfRef = dmScript::Ref(L, LUA_REGISTRYINDEX);
    extension->m_InitializeCalled = 1;
    lua_pushstring(L, "__TestScriptExtension__");
    lua_pushnumber(L, extension->m_SelfRef);
    dmScript::SetContextValue(context);
}

static void TestScriptExtensionUpdate(dmScript::HContext context)
{
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);

    TestScriptExtension* extension = GetTestScriptExtension(context);
    ++extension->m_UpdateCalled;
}

static void TestScriptExtensionFinalize(dmScript::HContext context)
{
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);

    TestScriptExtension* extension = GetTestScriptExtension(context);
    ++extension->m_FinalizedCalled = 1;
    lua_pushstring(L, "__TestScriptExtension__");
    lua_pushnil(L);
    dmScript::SetContextValue(context);
    dmScript::Unref(L, LUA_REGISTRYINDEX, extension->m_SelfRef);
}

struct TestScriptWorldContext
{
    TestScriptExtension* m_Extension;
    int m_SelfRef;
    float m_DeltaT;
    static uint32_t m_NewScriptWorldCalled;
    static uint32_t m_DeleteScriptWorldCalled;
    static uint32_t m_UpdateScriptWorldCalled;
    static uint32_t m_InitializeScriptInstancedCalled;
    static uint32_t m_FinalizeScriptInstancedCalled;
};

uint32_t TestScriptWorldContext::m_NewScriptWorldCalled = 0;
uint32_t TestScriptWorldContext::m_DeleteScriptWorldCalled = 0;
uint32_t TestScriptWorldContext::m_UpdateScriptWorldCalled = 0;
uint32_t TestScriptWorldContext::m_InitializeScriptInstancedCalled = 0;
uint32_t TestScriptWorldContext::m_FinalizeScriptInstancedCalled = 0;

static void TestScriptExtensionNewScriptWorld(dmScript::HScriptWorld script_world)
{
    dmScript::HContext context = dmScript::GetScriptWorldContext(script_world);
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);

    TestScriptExtension* extension = GetTestScriptExtension(context);
    ++extension->m_NewScriptWorldCalled = 1;

    TestScriptWorldContext* extension_world = (TestScriptWorldContext*)lua_newuserdata(L, sizeof(TestScriptWorldContext));
    memset(extension_world, 0, sizeof(TestScriptWorldContext));
    extension_world->m_Extension = extension;
    lua_pushstring(L, "__TestScriptWorldContext__");
    lua_pushvalue(L, -2);
    dmScript::SetScriptWorldContextValue(script_world);
    extension_world->m_SelfRef = dmScript::Ref(L, LUA_REGISTRYINDEX);
    ++extension_world->m_NewScriptWorldCalled;
}

static void TestScriptExtensionDeleteScriptWorld(dmScript::HScriptWorld script_world)
{
    dmScript::HContext context = dmScript::GetScriptWorldContext(script_world);
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);

    lua_pushstring(L, "__TestScriptWorldContext__");
    dmScript::GetScriptWorldContextValue(script_world);
    TestScriptWorldContext* extension_world = (TestScriptWorldContext*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    ++extension_world->m_DeleteScriptWorldCalled;
    ++extension_world->m_Extension->m_DeleteScriptWorldCalled;
    dmScript::Unref(L, LUA_REGISTRYINDEX, extension_world->m_SelfRef);
}

static void TestScriptExtensionUpdateScriptWorld(dmScript::HScriptWorld script_world, float dt)
{
    dmScript::HContext context = dmScript::GetScriptWorldContext(script_world);
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);

    lua_pushstring(L, "__TestScriptWorldContext__");
    dmScript::GetScriptWorldContextValue(script_world);
    TestScriptWorldContext* extension_world = (TestScriptWorldContext*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    ++extension_world->m_UpdateScriptWorldCalled;
    extension_world->m_DeltaT += dt;
    ++extension_world->m_Extension->m_UpdateScriptWorldCalled;
    ++extension_world->m_Extension->m_DeltaT += dt;
}

static void TestScriptExtensionInitializeScriptInstance(dmScript::HScriptWorld script_world)
{
    dmScript::HContext context = dmScript::GetScriptWorldContext(script_world);
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);

    lua_pushstring(L, "__TestScriptWorldContext__");
    dmScript::GetScriptWorldContextValue(script_world);
    TestScriptWorldContext* extension_world = (TestScriptWorldContext*)lua_touserdata(L, -1);
    ++extension_world->m_InitializeScriptInstancedCalled;
    ++extension_world->m_Extension->m_InitializeScriptInstancedCalled;
    lua_pushstring(L, "__TestScriptWorldContext__");
    lua_insert(L, -2);
    dmScript::SetInstanceContextValue(L);
}

static void TestScriptExtensionFinalizeScriptInstance(dmScript::HScriptWorld script_world)
{
    dmScript::HContext context = dmScript::GetScriptWorldContext(script_world);
    lua_State* L = dmScript::GetLuaState(context);
    DM_LUA_STACK_CHECK(L, 0);
    lua_pushstring(L, "__TestScriptWorldContext__");
    dmScript::GetInstanceContextValue(L);
    TestScriptWorldContext* extension_world = (TestScriptWorldContext*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    ++extension_world->m_FinalizeScriptInstancedCalled;
    ++extension_world->m_Extension->m_FinalizeScriptInstancedCalled;
    lua_pushstring(L, "__TestScriptWorldContext__");
    lua_pushnil(L);
    dmScript::SetInstanceContextValue(L);
}

TEST_F(ScriptTest, ScriptExtension)
{
    dmScript::HContext context = dmScript::NewContext(0x0, 0, true);

    static dmScript::ScriptExtension extension = 
    {
        TestScriptExtensionInitialize,
        TestScriptExtensionUpdate,
        TestScriptExtensionFinalize,
        TestScriptExtensionNewScriptWorld,
        TestScriptExtensionDeleteScriptWorld,
        TestScriptExtensionUpdateScriptWorld,
        TestScriptExtensionInitializeScriptInstance,
        TestScriptExtensionFinalizeScriptInstance
    };

    {
        lua_State* L = dmScript::GetLuaState(context);

        DM_LUA_STACK_CHECK(L, 0);
        int ref_count = dmScript::GetLuaRefCount();

        dmScript::RegisterScriptExtension(context, &extension);

        ASSERT_EQ(0u, TestScriptExtension::m_InitializeCalled);
        dmScript::Initialize(context);
        ASSERT_EQ(1u, TestScriptExtension::m_InitializeCalled);

        ASSERT_EQ(0u, TestScriptExtension::m_NewScriptWorldCalled);
        ASSERT_EQ(0u, TestScriptWorldContext::m_NewScriptWorldCalled);
        dmScript::HScriptWorld script_world = dmScript::NewScriptWorld(context);
        ASSERT_NE((dmScript::HScriptWorld)0x0, script_world);
        ASSERT_EQ(1u, TestScriptExtension::m_NewScriptWorldCalled);
        ASSERT_EQ(1u, TestScriptWorldContext::m_NewScriptWorldCalled);

        ASSERT_EQ(0u, TestScriptExtension::m_UpdateCalled);
        dmScript::Update(context);
        ASSERT_EQ(1u, TestScriptExtension::m_UpdateCalled);

        ASSERT_EQ(0u, TestScriptExtension::m_UpdateScriptWorldCalled);
        ASSERT_EQ(0u, TestScriptWorldContext::m_UpdateScriptWorldCalled);
        dmScript::UpdateScriptWorld(script_world, 0.16f);
        ASSERT_EQ(1u, TestScriptExtension::m_UpdateScriptWorldCalled);
        ASSERT_EQ(1u, TestScriptWorldContext::m_UpdateScriptWorldCalled);

        int instanceref = CreateAndPushInstance(L);

        lua_rawgeti(L, LUA_REGISTRYINDEX, instanceref);
        TestDummy* dummy = (TestDummy*)lua_touserdata(L, -1);
        lua_pop(L, 1);

        ASSERT_EQ(0u, TestScriptExtension::m_InitializeScriptInstancedCalled);
        ASSERT_EQ(0u, TestScriptWorldContext::m_InitializeScriptInstancedCalled);
        dmScript::InitializeInstance(script_world);
        ASSERT_EQ(1u, TestScriptExtension::m_InitializeScriptInstancedCalled);
        ASSERT_EQ(1u, TestScriptWorldContext::m_InitializeScriptInstancedCalled);

        ASSERT_EQ(0u, TestScriptExtension::m_FinalizeScriptInstancedCalled);
        ASSERT_EQ(0u, TestScriptWorldContext::m_FinalizeScriptInstancedCalled);
        dmScript::FinalizeInstance(script_world);
        ASSERT_EQ(1u, TestScriptExtension::m_FinalizeScriptInstancedCalled);
        ASSERT_EQ(1u, TestScriptWorldContext::m_FinalizeScriptInstancedCalled);

        lua_pushnil(L);
        dmScript::SetInstance(L);

        dmScript::Unref(L, LUA_REGISTRYINDEX, dummy->m_ContextTableReference);
        dmScript::Unref(L, LUA_REGISTRYINDEX, instanceref);

        ASSERT_EQ(0u, TestScriptExtension::m_DeleteScriptWorldCalled);
        ASSERT_EQ(0u, TestScriptWorldContext::m_DeleteScriptWorldCalled);
        dmScript::DeleteScriptWorld(script_world);

        ASSERT_EQ(1u, TestScriptExtension::m_DeleteScriptWorldCalled);
        ASSERT_EQ(1u, TestScriptWorldContext::m_DeleteScriptWorldCalled);

        ASSERT_EQ(0u, TestScriptExtension::m_FinalizedCalled);
        dmScript::Finalize(context);
        ASSERT_EQ(1u, TestScriptExtension::m_FinalizedCalled);

        ASSERT_EQ(ref_count, dmScript::GetLuaRefCount());
    }
    dmScript::DeleteContext(context);
}

TEST_F(ScriptTest, InstanceId)
{
    uintptr_t instanceid0 = dmScript::GetInstanceId(L);
    ASSERT_EQ(0, instanceid0);
    int instanceref1 = CreateAndPushInstance(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instanceref1);
    dmScript::SetInstance(L);
    uintptr_t instanceid1 = dmScript::GetInstanceId(L);
    int instanceref2 = CreateAndPushInstance(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instanceref2);
    dmScript::SetInstance(L);
    uintptr_t instanceid2 = dmScript::GetInstanceId(L);
    int instanceref3 = CreateAndPushInstance(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instanceref3);
    dmScript::SetInstance(L);
    uintptr_t instanceid3 = dmScript::GetInstanceId(L);
    lua_pushnil(L);
    dmScript::SetInstance(L);
    uintptr_t instanceid0_2 = dmScript::GetInstanceId(L);
    ASSERT_EQ(0, instanceid0_2);

    ASSERT_NE(0, instanceid1);
    ASSERT_NE(0, instanceid2);
    ASSERT_NE(0, instanceid3);

    ASSERT_NE(instanceid2, instanceid1);
    ASSERT_NE(instanceid3, instanceid1);
    ASSERT_NE(instanceid2, instanceid3);

    dmScript::Unref(L, LUA_REGISTRYINDEX, instanceref1);
    dmScript::Unref(L, LUA_REGISTRYINDEX, instanceref2);
    dmScript::Unref(L, LUA_REGISTRYINDEX, instanceref3);
}


int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    int ret = RUN_ALL_TESTS();
    return ret;
}
