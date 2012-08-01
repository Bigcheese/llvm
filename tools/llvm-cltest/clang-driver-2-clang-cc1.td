// NOTE: For this to work TableGen will need to support duplicate includes.
include "clang-driver.td"
include "clang-cc1.td"

def : Transform< [(clang_f_strict_enums), (not clang_f_no_strict_enums)]
                , [(clang_cc1_f_strict_enums)]>;

def : Transform< [(clang_f_template_depth (str:$v0))]
                , [(clang_cc1_f_template_depth (str:$v0))]>;
// Since this case is common, there would probably be a:
def : Forward<clang_f_template_depth, clang_cc1_f_template_depth>;
// This would simply copy the Argument values.

def : Forward<clang_f_fp_contract, clang_cc1_f_fp_contract>;

def : Transform< [(clang_f_fast_math), (not clang_f_fp_contract)]
                , [(clang_cc1_f_fp_contract (str "fast"))]>;
