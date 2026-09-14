// TestResponseParser.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <boost/archive/text_woarchive.hpp>
#include <boost/detail/lightweight_test.hpp>
#include <boost/thread.hpp>
#include <ResponseParser.h>
#include <WeaselUtility.h>
#include <atomic>
#include <sstream>
#include <string>
#include <vector>

// 按服务端 _Respond 的协议形态生成 ctx.cand 归档行
static std::wstring make_cand_line(const weasel::CandidateInfo& cinfo) {
  std::wstringstream ss;
  boost::archive::text_woarchive oa(ss);
  oa << cinfo;
  return L"ctx.cand=" + ss.str() + L"\n";
}

void test_1() {
  WCHAR resp[] = L"action=noop\n";
  DWORD len = wcslen(resp);
  std::wstring commit;
  weasel::Context ctx;
  weasel::Status status;
  weasel::ResponseParser parser(&commit, &ctx, &status);
  parser(resp, len);
  BOOST_TEST(commit.empty());
  BOOST_TEST(ctx.empty());
}

void test_2() {
  WCHAR resp[] =
      L"action=commit\n"
      L"commit=教這句話上屏=3.14\n";
  DWORD len = wcslen(resp);
  std::wstring commit;
  weasel::Context ctx;
  weasel::Status status;
  ctx.aux.str = L"從前的值";
  weasel::ResponseParser parser(&commit, &ctx, &status);
  parser(resp, len);
  BOOST_TEST(commit == L"教這句話上屏=3.14");
  BOOST_TEST(ctx.preedit.empty());
  BOOST_TEST(ctx.aux.str == L"從前的值");
  BOOST_TEST(ctx.cinfo.candies.empty());
}

void test_3() {
  WCHAR resp[] =
      L"action=ctx\n"
      L"ctx.preedit=寫作串=3.14\n"
      L"ctx.aux=sie'zuoh'chuan=3.14\n";
  DWORD len = wcslen(resp);
  std::wstring commit;
  weasel::Context ctx;
  weasel::Status status;
  weasel::ResponseParser parser(&commit, &ctx, &status);
  parser(resp, len);
  BOOST_TEST(commit.empty());
  BOOST_TEST(ctx.preedit.str == L"寫作串=3.14");
  BOOST_TEST(ctx.preedit.attributes.empty());
  BOOST_TEST(ctx.aux.str == L"sie'zuoh'chuan=3.14");
}

// 当前协议：ctx.preedit.cursor 为 start,end,cursor 三段，
// ctx.cand 为单行 boost 归档（服务端 _Respond 的实际输出形态）
void test_4() {
  weasel::CandidateInfo ci;
  ci.currentPage = 0;
  ci.totalPages = 1;
  ci.highlighted = 1;
  ci.candies.push_back(weasel::Text{L"候選甲"});
  ci.candies.push_back(weasel::Text{L"候選乙"});

  std::wstring resp = L"action=commit,ctx\n"
                      L"ctx.preedit=候選乙=3.14\n"
                      L"ctx.preedit.cursor=0,3,3\n" +
                      make_cand_line(ci);
  std::vector<WCHAR> buf(resp.begin(), resp.end());
  buf.push_back(L'\0');
  DWORD len = buf.size() - 1;

  std::wstring commit;
  weasel::Context ctx;
  weasel::Status status;
  weasel::ResponseParser parser(&commit, &ctx, &status);
  parser(buf.data(), len);
  BOOST_TEST(commit.empty());
  BOOST_TEST(ctx.preedit.str == L"候選乙=3.14");
  BOOST_ASSERT(1 == ctx.preedit.attributes.size());
  weasel::TextAttribute attr0 = ctx.preedit.attributes[0];
  BOOST_TEST_EQ(weasel::HIGHLIGHTED, attr0.type);
  BOOST_TEST_EQ(0, attr0.range.start);
  BOOST_TEST_EQ(3, attr0.range.end);
  BOOST_TEST_EQ(3, attr0.range.cursor);
  BOOST_TEST(ctx.aux.empty());
  weasel::CandidateInfo& c = ctx.cinfo;
  BOOST_ASSERT(2 == c.candies.size());
  BOOST_TEST(c.candies[0].str == L"候選甲");
  BOOST_TEST(c.candies[1].str == L"候選乙");
  BOOST_TEST_EQ(1, c.highlighted);
  BOOST_TEST_EQ(0, c.currentPage);
  BOOST_TEST_EQ(1, c.totalPages);
}

// 截断的 cursor 值（缺 cursor 段）不得越界读，属性整体丢弃
void test_5() {
  const WCHAR* responses[] = {
      L"action=ctx\nctx.preedit=寫作串\nctx.preedit.cursor=0,3\n",
      L"action=ctx\nctx.preedit=寫作串\nctx.preedit.cursor=2\n",
  };
  for (auto* resp : responses) {
    std::vector<WCHAR> buf(resp, resp + wcslen(resp) + 1);
    std::wstring commit;
    weasel::Context ctx;
    weasel::Status status;
    weasel::ResponseParser parser(&commit, &ctx, &status);
    parser(buf.data(), wcslen(resp));
    BOOST_TEST(ctx.preedit.str == L"寫作串");
    BOOST_TEST(ctx.preedit.attributes.empty());
  }
}

// 有 context 无 config 的调用方：config 键必须被忽略而不是空指针崩溃；
// 提供 config 时正常落地
void test_6() {
  WCHAR resp[] =
      L"action=ctx,config\n"
      L"ctx.aux=sie'zuoh'chuan\n"
      L"config.inline_preedit=1\n";
  DWORD len = wcslen(resp);

  std::wstring commit;
  weasel::Context ctx;
  weasel::Status status;
  weasel::ResponseParser parser(&commit, &ctx, &status);
  parser(resp, len);
  BOOST_TEST(ctx.aux.str == L"sie'zuoh'chuan");

  weasel::Config config;  // inline_preedit 默认 false
  weasel::Context ctx2;
  weasel::ResponseParser parser2(&commit, &ctx2, &status, &config);
  parser2(resp, len);
  BOOST_TEST(config.inline_preedit);
}

// escape_string/unescape_string 转义往返（B36）：只动 \\、\n、\t 三种字符，
// 其余（含用户输入的 %、= 等）原样透传不膨胀；逃逸语义与旧 stringstream
// 实现逐字符一致，协议两侧对称
void test_7() {
  const std::wstring inputs[] = {
      L"",
      L"plain text",
      L"line1\nline2\tend",
      L"back\\slash \\\\ nested",
      L"100% \\path\\to\\file %x00 \\n \\t",
      L"候選=3.14\t註釋\\x\n写作串",
      L"trailing escape\\",
  };
  for (const auto& s : inputs) {
    std::wstring roundtrip = unescape_string(escape_string(s));
    BOOST_TEST(roundtrip == s);
  }
  // 窄字符特化与宽字符特化行为一致（输入限 ASCII）
  const std::string narrow = "line1\nline2\tback\\slash %x00";
  BOOST_TEST(unescape_string(escape_string(narrow)) == narrow);

  BOOST_TEST(escape_string(std::wstring(L"a\nb")) == L"a\\nb");
  BOOST_TEST(escape_string(std::wstring(L"a\tb")) == L"a\\tb");
  BOOST_TEST(escape_string(std::wstring(L"a\\b")) == L"a\\\\b");
  // 用户输入的 % 序列不膨胀
  std::wstring pct(L"%x00 %s %%");
  BOOST_TEST(escape_string(pct) == pct);

  // 非约定转义还原为裸字符；结尾孤立反斜杠丢弃（与旧实现一致）
  BOOST_TEST(unescape_string(std::wstring(L"\\a\\b\\\\")) == L"ab\\");
  BOOST_TEST(unescape_string(std::wstring(L"end\\")) == L"end");
}

// B5: 损坏/截断的归档行必须被记录并丢弃——不得弹模态框（输入线程冻结），
// 不得把异常抛出解析器；候选数超过渲染上限（100，见 WeaselUI
// MAX_CANDIDATES_COUNT）视为损坏数据，整体丢弃
void test_8() {
  const WCHAR* responses[] = {
      L"action=ctx\nctx.cand=garbage header\n",              // 非归档数据
      L"action=ctx\nctx.cand=22 serialization::archive\n",   // 截断归档
      L"action=ctx\nctx.cand=99 serialization::archive 15\n",  // 版本不匹配
      L"action=style\nstyle=not an archive\n",               // Styler 同路径
  };
  for (auto* resp : responses) {
    std::vector<WCHAR> buf(resp, resp + wcslen(resp) + 1);
    std::wstring commit;
    weasel::Context ctx;
    weasel::Status status;
    weasel::ResponseParser parser(&commit, &ctx, &status);
    parser(buf.data(), wcslen(resp));
    BOOST_TEST(ctx.cinfo.candies.empty());
  }

  // 101 个候选：归档合法但超限，cinfo 整体清空
  weasel::CandidateInfo ci;
  ci.currentPage = 0;
  ci.totalPages = 1;
  ci.highlighted = 0;
  for (int i = 0; i < 101; ++i)
    ci.candies.push_back(weasel::Text{L"候"});
  std::wstring resp = L"action=ctx\n" + make_cand_line(ci);
  std::vector<WCHAR> buf(resp.begin(), resp.end());
  buf.push_back(L'\0');
  std::wstring commit;
  weasel::Context ctx;
  weasel::Status status;
  weasel::ResponseParser parser(&commit, &ctx, &status);
  parser(buf.data(), buf.size() - 1);
  BOOST_TEST(ctx.cinfo.candies.empty());
}

// B21: 多线程并发首次构造 ResponseParser（s_factories 曾为无锁懒初始化，
// 并发 insert 属 UB）。必须最先执行：工厂表只在本进程首次构造时初始化，
// 排在其他用例之后并发首init就无从谈起；起跑线屏障让所有线程同时进入构造
void test_9() {
  const int kThreads = 8;
  weasel::CandidateInfo ci;
  ci.candies.push_back(weasel::Text{L"候選甲"});
  ci.candies.push_back(weasel::Text{L"候選乙"});
  const std::wstring sample =
      L"action=ctx,commit\n"
      L"ctx.preedit=寫作串\n" +
      make_cand_line(ci) + L"commit=上屏\n";

  std::vector<int> ok(kThreads, 0);
  std::atomic<int> waiting{0};
  boost::thread_group group;
  for (int i = 0; i < kThreads; ++i) {
    group.create_thread([&sample, &ok, &waiting, i, kThreads] {
      ++waiting;
      while (waiting.load() < kThreads)
        ;  // 起跑线：保证 8 线程同时首次构造
      std::vector<WCHAR> buf(sample.begin(), sample.end());
      buf.push_back(L'\0');
      std::wstring commit;
      weasel::Context ctx;
      weasel::Status status;
      weasel::ResponseParser parser(&commit, &ctx, &status);
      parser(buf.data(), buf.size() - 1);
      // BOOST_TEST 计数器非线程安全，线程内只记录结果
      ok[i] = (ctx.cinfo.candies.size() == 2 && commit == L"上屏") ? 1 : 0;
    });
  }
  group.join_all();
  for (int i = 0; i < kThreads; ++i)
    BOOST_TEST(ok[i] == 1);
}

// B31: getUsername must return the account name; the second GetUserName
// call is now checked, so a failure returns the empty string instead of
// building a wstring from an uninitialized buffer (failure injection is
// not possible without refactoring the win32 call away, so the normal
// path guards against regressions).
void test_10() {
  std::wstring user = weasel::getUsername();
  BOOST_TEST(!user.empty());
  // the querying call must have sized the buffer to fit it exactly
  DWORD len = 0;
  GetUserName(NULL, &len);
  BOOST_TEST(user.size() == len - 1);  // len includes the terminator
}

// B35④: DebugStream<<(std::string) decodes its input as utf-8, matching
// the const char* branch; the decoder is u8tow. OutputDebugString cannot
// be captured, so the shared decoder is asserted directly instead.
void test_11() {
  const std::wstring zh = L"小狼毫";
  std::string utf8 = wtou8(zh);
  BOOST_TEST(!utf8.empty());
  BOOST_TEST(u8tow(utf8) == zh);  // utf-8 decode restores the text
  BOOST_TEST(acptow(utf8) != zh);  // the old ACP decode mangled it
}

int _tmain(int argc, _TCHAR* argv[]) {
  test_9();  // B21: 首个构造须发生在多线程里，故最先执行
  test_1();
  test_2();
  test_3();
  test_4();
  test_5();
  test_6();
  test_7();
  test_8();
  test_10();
  test_11();

  return boost::report_errors();
}
