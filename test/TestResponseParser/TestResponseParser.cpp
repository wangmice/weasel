// TestResponseParser.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <boost/archive/text_woarchive.hpp>
#include <boost/detail/lightweight_test.hpp>
#include <ResponseParser.h>
#include <WeaselUtility.h>
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

int _tmain(int argc, _TCHAR* argv[]) {
  test_1();
  test_2();
  test_3();
  test_4();
  test_5();
  test_6();
  test_7();

  return boost::report_errors();
}
