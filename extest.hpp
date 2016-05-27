#ifndef EXTEST_HPP
#define EXTEST_HPP
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <vector>
#include <string>
#include <algorithm>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <execinfo.h>
#include <string>
#include <cxxabi.h>
#include <dlfcn.h>

typedef void (*ExTestCase)(void);

template <typename T>
const std::string extest_toString(const T& v)
{
    return std::to_string(v);
}

template <>
const std::string extest_toString(const std::string& v)
{
    return v;
}

class TestCaseHolder
{
  public:
    TestCaseHolder(ExTestCase tc, const char* suite, const char* tcname) : tc(tc), suite(suite), tcname(tcname)
    {

    }
    void run()
    {
        tc();
    }
    const char* getSuiteName() const
    {
        return suite;
    }
    const char* getTestCaseName() const
    {
        return tcname;
    }
  private:
    ExTestCase tc;
    const char* suite;
    const char* tcname;
};

std::vector<TestCaseHolder>& exTestCases()
{
    static std::vector<TestCaseHolder> testCases;
    return testCases;
}
static void registerTest(ExTestCase testCase, const char* suite, const char* tcname)
{

    exTestCases().push_back(TestCaseHolder(testCase, suite, tcname));
}

#define TEST(testSuite, testCase) \
    void ExTest_ ## testSuite ## _ ## testCase(void);  \
    __attribute__((constructor)) void ExTest_Register_ ## testSuite ## _ ## testCase(void) \
    { \
        registerTest(ExTest_ ## testSuite ## _ ## testCase, #testSuite, #testCase); \
    } \
    void ExTest_ ## testSuite ## _ ## testCase(void) 

class TestCaseBuffer
{
  public:
    static TestCaseBuffer& instance()
    {
        static TestCaseBuffer buffer;
        return buffer;
    }
    void pushPass()
    {
        passed++;
    }
    void pushFail(bool isAssert)
    {
        hadAssertion |= isAssert;
        failed++;
        if (isAssert)
        {
            throw 1;
        }
    }
    int getResult()
    {
        if (failed != 0)
        {
            return hadAssertion?2:1;
        }
        return 0;
    }
    void clear()
    {
        hadAssertion = false;
        passed = 0;
        failed = 0;
        longBuffer = "";
        quickBuffer[0] = 0;
    }
    void printf(const char* fmt, ...)
    {
        va_list args;
        va_start(args,fmt);
        int l = vsnprintf(quickBuffer, 4095, fmt, args);
        va_end(args);
        if (l > 4095)
        {
            l = 4095;
        }
        quickBuffer[l] = 0;
        longBuffer += quickBuffer;
    }
    const char* getBuffer()
    {
        return longBuffer.c_str();
    }
  private:
    char quickBuffer[4096];
    std::string longBuffer;
    int failed;
    int passed;
    bool hadAssertion;
};

enum Override
{
    Undef = 0,
    Eq,
    Neq,
    Gt,
    Lt,
    Have,
    NotHave,
};
class Exoper
{
  public:
    Exoper(Override type):type(type)
    {
    }
    Override getType()
    {
        return type;
    }
  private:
    Override type;
};

template <typename T>
class expect
{
  public:
    expect():valid(false)
    {
        override = Undef;
        matching = Undef;
    }
    expect& operator()(const char* file, int line, bool isAssert)
    {
        this->file = file;
        this->line = line;
        this->isAssert = isAssert;
        return *this;
    }
    expect& operator>>(const T & x)
    {
        if (valid)
        {
            valid = false;
        }
        else
        {
            valid = true;
        }
        val = &x;
        return *this; 
    }
    expect& operator>>(Exoper x)
    {
        matching = x.getType();
        return *this;
    }
    expect& operator<< (Exoper x)
    {
        override = x.getType();
        return *this;
    }
    expect& operator<< (const T& y)
    {
        other = extest_toString(y);
        switch (override)
        {
            case Undef:
                /* FALLTHROUGH */
            case Eq:
                operation = "equal to";
                check(value() == y);
                return *this;
            case Neq:
                operation = "not equal to";
                check(value() != y);
                return *this;
            case Gt:
                operation = "greater than";
                check(value() > y);
                return *this;
            case Lt:
                operation = "less than";
                check(value() < y);
                return *this;
            case NotHave:
                /* FALLTHROUGH */
            case Have:
                TestCaseBuffer::instance().printf("Have is not allowed for homogeneous checking at %s:%d!\n",file,line);
                TestCaseBuffer::instance().pushFail(isAssert);
                // This is the homogeneous case. No have is allowed!
                return *this;
        }
        return *this;
    }
    expect& operator<< (const char* y)
    {
        return operator<<(std::string(y));
    }
    template <typename T2>
    expect& operator<< (const T2& y)
    {
        other = extest_toString(y);
        switch (override)
        {
            case Have:
                operation = "have";
                check2(std::find(value().begin(), value().end(), y) != value().end());
                return *this;
            case NotHave:
                operation = "not have";
                check2(std::find(value().begin(), value().end(), y) == value().end());
                return *this;
            default:
                TestCaseBuffer::instance().printf("Not allowed  %s:%d\n",file,line);
                TestCaseBuffer::instance().pushFail(isAssert);
                return *this;
        }
        return *this;
    }
    void check2(bool b)
    {
        if (!valid)
        {
            TestCaseBuffer::instance().printf("You messed up something at  %s:%d\n",file,line);
            TestCaseBuffer::instance().pushFail(isAssert);
            return;
        }
        if (b)
        {
            TestCaseBuffer::instance().pushPass();
            TestCaseBuffer::instance().printf("\t%s passed at %s:%d \x1b[32msucceeded\x1b[0m.\n",expectType(),file,line);
        }
        else
        {
            TestCaseBuffer::instance().printf("\t%s failed at %s:%d \x1b[31mfailed\x1b[0m: \x1b[1;33mExpected container to %s `%s'\x1b[0m\n",expectType(),file,line,operation.c_str(),other.c_str());
            TestCaseBuffer::instance().pushFail(isAssert);
        }
        valid = false;
        override = Undef;
    }
    void check(bool b)
    {
        if (!valid)
        {
            TestCaseBuffer::instance().printf("You messed up something at  %s:%d\n",file,line);
            TestCaseBuffer::instance().pushFail(isAssert);
            return;
        }
        if (b)
        {
            TestCaseBuffer::instance().pushPass();
            TestCaseBuffer::instance().printf("\t%s passed at %s:%d \x1b[32msucceeded\x1b[0m.\n",expectType(),file,line);
        }
        else
        {
            TestCaseBuffer::instance().printf("\t%s failed at %s:%d \x1b[31mfailed\x1b[0m: \x1b[1;33mExpected `%s' to be %s `%s'\x1b[0m\n",expectType(),file,line,extest_toString(value()).c_str(),operation.c_str(),other.c_str());
            TestCaseBuffer::instance().pushFail(isAssert);
        }
        valid = false;
        override = Undef;
    }
  private:
    const T& value() 
    {
        return *val;
    }
    const char* expectType()
    {
        return isAssert?"Assertion":"Expect";
    }
    Override override;
    Override matching;
    bool valid;
    const T * val;
    std::string other;
    std::string operation;
    const char* file;
    int line;
    bool isAssert;
};

class ExpectGenerator
{
  public:
    ExpectGenerator(const char* file, int line, bool isAssert):file(file),line(line),isAssert(isAssert)
    {
    }
    template<typename T> 
    expect<T> operator>>(const T & x)
    {
        return expect<T>()(file,line,isAssert) >> x;
    }
  private:
    const char* file;
    int line;
    bool isAssert;
};

#define to 
#define be ) <<
#define have ) << Exoper(Have) <<
#define nothave ) << Exoper(NotHave) <<
#define equal Exoper(Eq) << 
#define notequal Exoper(Neq) <<
#define than
#define greater Exoper(Gt) <<
#define less Exoper(Lt) <<
#define Expect ( ExpectGenerator(__FILE__,__LINE__,false) >>
#define Assert ( ExpectGenerator(__FILE__,__LINE__,true) >>
#define Everything for (auto ___everything
#define in :
#define should ) Expect ___everything
#define must ) Assert ___everything

#ifdef EX_SELFTEST
TEST(ExTest, SelfTest)
{
    int x = 2;
    Expect x be 2;
    Expect x to be 2;
    Expect x to be equal to 2; 
    Expect x to be notequal to 6; 
    Expect x to be greater than 1; 
    Expect x to be less than 3;
    std::vector<int> v;
    v.push_back(2);
    Expect v to have 2;
    Expect v to nothave 4;
    Everything in v should be greater than 1;
    Everything in v must be greater than 1;
}
TEST(ExTest, FailingExpect)
{
    int x = 2;
    Expect x to be equal to 3; 
    Expect x to be notequal to 2; 
    Expect x to be greater than 3; 
    Expect x to be less than 1; 
    std::vector<int> v;
    v.push_back(2);
    Expect v to have 4;
    Expect v to nothave 2;
    Everything in v should be less than 1;
}
TEST(ExTest, FailingAssertion)
{
    int x = 2;
    Assert x to be equal to 3; 
}
TEST(ExTest, DivByZero)
{
    volatile int y = 0;
    volatile int x = 2/y;
    Expect (int)x to be equal to 2; 
}
TEST(ExTest, SegfaultRead)
{
    volatile char* x = 0;
    char d = *x;
    d -= 1;
}
TEST(ExTest, SegfaultWrite)
{
    volatile char* x = 0;
    *x = 2;
}

int factorial(int x)
{
    if (x == 1)
    {
        return 1;
    }
    return x*factorial(x);
}

TEST(ExTest, StackOverflow)
{
    int f = factorial(2);
}

#endif

const char* testReason(int status)
{
    if (status == 0) return "passed.";
    if (status == 1) return "a failed expect.";
    if (status == 2) return "a failed assertion.";
    if ((status & 0x7f) == 8) return "a programming error: \x1b[1;34mdivision by zero\x1b[0m.";
    if ((status & 0x7f) == 11) return "a programming error: \x1b[1;34msegmentation fault\x1b[0m.";
    return "a programming error: \x1b[1;34munknown error\x1b[0m.";
}

#ifdef UNITTEST
int main(void)
{
    int passed = 0;
    int failed = 0;
    int aborted = 0;
    for (auto tc : exTestCases())
    {
        int pipefd[2];
        pipe(pipefd);
        pid_t pid = fork();
        if (pid == 0)
        {
            ptrace(PTRACE_TRACEME,0,0,0);
            close(pipefd[0]);
            TestCaseBuffer::instance().clear();
            try
            {
                tc.run();
            }
            catch (int x)
            {
                // Assertion failed.
            }
            const char* buffer = TestCaseBuffer::instance().getBuffer();
            write(pipefd[1],buffer,strlen(buffer) + 1);
            close(pipefd[1]);
            exit(TestCaseBuffer::instance().getResult());
        }
        else
        {
            close(pipefd[1]);
            int status;
            char buff[4096];
            buff[0] = 0;
            do
            {
                wait(&status);
                if (WIFSIGNALED(status))
                {
                    int signum = WSTOPSIG(status);
                    ptrace(PTRACE_CONT, pid, 0, signum);
                }
                if (WIFSTOPPED(status))
                {
                    user_regs_struct regs;
                    int signum = WSTOPSIG(status);
                    ptrace(PTRACE_GETREGS, pid, 0, &regs);
                    void* ptr = reinterpret_cast<void*>(regs.rip);
                    char** symbols = backtrace_symbols(&ptr,1);
                    std::string symbolString(symbols[0]);
                    free(symbols);
                    ssize_t opening = symbolString.find("(");
                    ssize_t closing = symbolString.find(")");
                    std::string fileName = symbolString.substr(0, opening);
                    std::string bt = symbolString.substr(opening + 1, closing - opening - 1);
                    ssize_t plus = bt.find("+");
                    std::string mangledFun = bt.substr(0, plus);
                    std::string mangledOffset = bt.substr(plus+1);

                    char funcname[4096];
                    size_t funcnamesize = 4096;
                    int stat;
                    abi::__cxa_demangle(mangledFun.c_str(), funcname, &funcnamesize, &stat);
                    Dl_info info;
                    dladdr(ptr, &info);
                    if (info.dli_saddr != 0)
                    {
                        ssize_t diff = reinterpret_cast<uintptr_t>(info.dli_saddr) - reinterpret_cast<uintptr_t>(info.dli_fbase);
                        snprintf(buff, 4096, "\tProgram error in file \x1b[1;33m%s\x1b[0m at address %08p+%08p [%08p] in \x1b[1;36m%s\x1b[0;m\n",info.dli_fname, info.dli_fbase, diff, info.dli_saddr, funcname);
                    }
                    else
                    {
                        snprintf(buff, 4096, "\tProgram error in file \x1b[1;33m%s\x1b[0m. Unable to determine context! Please recompile with \x1b[1;33m-rdynamic\x1b[0;m!\n", fileName.c_str());
                    }
                    failed--;
                    aborted++;
                    break;
                }
            } while (!WIFEXITED(status));
            status>>=8;
            ptrace(PTRACE_DETACH, pid, 0, 0);
            printf("\n");
            ssize_t rl = 0;
                printf("Test case %s::%s is ",tc.getSuiteName(), tc.getTestCaseName());
            if (status == 0)
            {
                passed++;
                printf("\x1b[32mpassed\x1b[0m.\n");
            }
            else
            {
                failed++;
                printf("\x1b[31mfailed\x1b[0m due to %s\n",testReason(status));
                printf("%s", buff);
                while ((rl =read(pipefd[0],buff,4096)))
                {
                    printf("%s",buff);
                }
            }
            close(pipefd[0]);
        }
    }
    printf("\n \u250c\u2500\u2500\u2500 Test summary \u2500\u2500\u2500\u2512\n");
    const char* fmt = " \u2502 %-13s \x1b[%sm%4d\x1b[0m \u2503\n";
    if (passed > 0)
    {
        printf(fmt,"Passed", "1;32", passed);
    }
    if (failed > 0)
    {
        printf(fmt,"Failed", "1;31", failed);
    }
    if (aborted > 0)
    {
        printf(fmt,"Aborted", "1;34", aborted);
    }
    printf(" \u2515\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u251b\n");
    if (failed+aborted > 0)
    {
        return 1;
    }
    return 0;
}
#endif
#endif
