// TestResponseParser.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <boost/archive/text_woarchive.hpp>
#include <boost/detail/lightweight_test.hpp>
#include <ResponseParser.h>
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

int _tmain(int argc, _TCHAR* argv[]) {
  test_1();
  test_2();
  test_3();
  test_4();
  test_5();

  return boost::report_errors();
}
