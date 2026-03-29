#include "test_common.hpp"

namespace zephyr_tests {

void test_corpus_scripts() {
    struct CorpusCase {
        std::filesystem::path path;
        std::string expected_output;
    };

    const auto corpus_root = std::filesystem::current_path() / "tests" / "corpus";
    const std::vector<CorpusCase> cases = {
        {corpus_root / "01_basic_arithmetic.zph", "30\n195"},
        {corpus_root / "02_string_interpolation.zph", "Hello Zephyr v1"},
        {corpus_root / "03_optional_chaining.zph", "42\nnil"},
        {corpus_root / "04_pattern_matching.zph", "zero\nsmall\nnegative\nlarge"},
        {corpus_root / "05_coroutine.zph", "0\n1\n2"},
        {corpus_root / "06_traits.zph", "Meow, I am Kitty"},
        {corpus_root / "07_generics.zph", "42\nhello\n10"},
    };

    for (const auto& corpus_case : cases) {
        require(std::filesystem::exists(corpus_case.path), "corpus: missing file " + corpus_case.path.string());
        run_corpus_case(corpus_case.path, corpus_case.expected_output);
    }
}

}  // namespace zephyr_tests
