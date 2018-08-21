#include "stdafx.h"

TEST(B64Codec, TestSample)
{
    const char *origin_str = "Send reinforcements";
    ASSERT_STREQ("U2VuZCByZWluZm9yY2VtZW50cw==", crx::base64_encode(origin_str, strlen(origin_str)).c_str());
    const char *base64_str = "U2VuZCByZWluZm9yY2VtZW50cw==";
    ASSERT_STREQ("Send reinforcements", crx::base64_decode(base64_str, strlen(base64_str)).c_str());

    origin_str = "Now is the time for all good coders\nto learn cpp";
    ASSERT_STREQ("Tm93IGlzIHRoZSB0aW1lIGZvciBhbGwgZ29vZCBjb2RlcnMKdG8gbGVhcm4gY3Bw",
            crx::base64_encode(origin_str, strlen(origin_str)).c_str());
    base64_str = "Tm93IGlzIHRoZSB0aW1lIGZvciBhbGwgZ29vZCBjb2RlcnMKdG8gbGVhcm4gY3Bw";
    ASSERT_STREQ("Now is the time for all good coders\nto learn cpp",
            crx::base64_decode(base64_str, strlen(base64_str)).c_str());

    origin_str = "This is line one\nThis is line two\nThis is line three\nAnd so on...\n";
    ASSERT_STREQ("VGhpcyBpcyBsaW5lIG9uZQpUaGlzIGlzIGxpbmUgdHdvClRoaXMgaXMgbGluZSB0aHJlZQpBbmQgc28gb24uLi4K",
            crx::base64_encode(origin_str, strlen(origin_str)).c_str());
    base64_str = "VGhpcyBpcyBsaW5lIG9uZQpUaGlzIGlzIGxpbmUgdHdvClRoaXMgaXMgbGluZSB0aHJlZQpBbmQgc28gb24uLi4K";
    ASSERT_STREQ("This is line one\nThis is line two\nThis is line three\nAnd so on...\n",
            crx::base64_decode(base64_str, strlen(base64_str)).c_str());
}

TEST(B64Codec, TestEncode64)
{
    ASSERT_STREQ("", crx::base64_encode("", 0).c_str());
    ASSERT_STREQ("AA==", crx::base64_encode("\0", 1).c_str());
    ASSERT_STREQ("AAA=", crx::base64_encode("\0\0", 2).c_str());
    ASSERT_STREQ("AAAA", crx::base64_encode("\0\0\0", 3).c_str());
    ASSERT_STREQ("/w==", crx::base64_encode("\377", 1).c_str());
    ASSERT_STREQ("//8=", crx::base64_encode("\377\377", 2).c_str());
    ASSERT_STREQ("////", crx::base64_encode("\377\377\377", 3).c_str());
    ASSERT_STREQ("/+8=", crx::base64_encode("\xff\xef", 2).c_str());
}

TEST(B64Codec, TestDecode64)
{
    ASSERT_STREQ("", crx::base64_decode("", 0).c_str());
    ASSERT_STREQ("\0", crx::base64_decode("AA==", 4).c_str());
    ASSERT_STREQ("\0\0", crx::base64_decode("AAA=", 4).c_str());
    ASSERT_STREQ("\0\0\0", crx::base64_decode("AAAA", 4).c_str());
    ASSERT_STREQ("\377", crx::base64_decode("/w==", 4).c_str());
    ASSERT_STREQ("\377\377", crx::base64_decode("//8=", 4).c_str());
    ASSERT_STREQ("\377\377\377", crx::base64_decode("////", 4).c_str());
    ASSERT_STREQ("\xff\xef", crx::base64_decode("/+8=", 4).c_str());
}

TEST(B64Codec, TestDecode64InvalidArgs)
{
    ASSERT_STREQ("", crx::base64_decode("^", 1).c_str());
    ASSERT_STREQ("", crx::base64_decode("A", 1).c_str());
    ASSERT_STREQ("", crx::base64_decode("A^", 2).c_str());
    ASSERT_STREQ("", crx::base64_decode("AA", 2).c_str());
    ASSERT_STREQ("", crx::base64_decode("AA=", 3).c_str());
    ASSERT_STREQ("", crx::base64_decode("AAA", 3).c_str());
    ASSERT_STREQ("", crx::base64_decode("AA=x", 4).c_str());
    ASSERT_STREQ("", crx::base64_decode("AAA^", 4).c_str());
    ASSERT_STREQ("", crx::base64_decode("AB==", 4).c_str());
    ASSERT_STREQ("", crx::base64_decode("AAB=", 4).c_str());
    ASSERT_STREQ("", crx::base64_decode("AA===", 5).c_str());
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}