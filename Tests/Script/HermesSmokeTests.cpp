#include <lumora/script/Script.h>

#include <gtest/gtest.h>

TEST(HermesSmoke, EvaluatesArithmetic) { EXPECT_EQ(lumora::script::EvalToString("1 + 2"), "3"); }
TEST(HermesSmoke, SupportsModernSyntax) {
    EXPECT_EQ(lumora::script::EvalToString("class A { #x = 2; get x() { return this.#x; } } const f = async () => 1; `${new A().x}`"), "2");
}
TEST(HermesSmoke, ReportsSyntaxErrors) { EXPECT_TRUE(lumora::script::EvalToString("let =").starts_with("Error:")); }
TEST(HermesSmoke, Utf8RoundTrip) { EXPECT_EQ(lumora::script::EvalToString("'héllo 世界 👋'"), "héllo 世界 👋"); }
TEST(HermesSmoke, ReportsThrownErrorMessage) { EXPECT_EQ(lumora::script::EvalToString("throw new Error('boom')"), "Error: boom"); }
