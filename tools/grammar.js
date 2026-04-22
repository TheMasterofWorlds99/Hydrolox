/**
 * @file Tree-sitter grammar for the low level Hydrolox programming language
 * @author TheMasterofWorlds99 <eleyow@outlook.com>
 * @license Apache-2.0
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

export default grammar({
  name: "hydrolox",

  extras: $ => [
    /\s/,
    $.comment
  ],

  // This section tells Tree-sitter how to handle the for-loop ambiguity
  conflicts: $ => [
    [$.for_stmt, $._for_init],
    [$._expression, $.assignment_expr],
  ],

  word: $ => $.identifier,

  rules: {
    source_file: $ => repeat($._statement),

    _statement: $ => choice(
      $.function_decl,
      $.extern_decl,
      $.struct_decl,
      $.variable_decl,
      $.if_stmt,
      $.while_stmt,
      $.for_stmt,
      $.return_stmt,
      $.expr_stmt
    ),

    expr_stmt: $ => seq($._expression, ";"),

    function_decl: $ =>
      seq("func", $.type, $.identifier, "(", optional($.param_list), ")", $.block),

    extern_decl: $ =>
      seq("extern", "func", $.type, $.identifier, "(", optional($.extern_param_list), ")", ";"),

    param_list: $ => seq($.param, repeat(seq(",", $.param))),
    param: $ => seq($.identifier, ":", $.type),

    extern_param_list: $ => seq($.extern_param, repeat(seq(",", $.extern_param))),
    extern_param: $ => seq(optional($.identifier), ":", $.type),

    struct_decl: $ =>
      seq("struct", $.identifier, "{", repeat($.struct_field), "}"),

    struct_field: $ => seq($.identifier, ":", $.type, ";"),

    variable_decl: $ =>
      seq($.identifier, ":", $.type, optional(seq("=", $._expression)), ";"),

    if_stmt: $ =>
      seq("if", "(", $._expression, ")", $.block, optional(seq("else", $.block))),

    while_stmt: $ => seq("while", "(", $._expression, ")", $.block),

    for_stmt: $ =>
      seq(
        "for",
        "(",
        optional($._for_init),
        $._expression,
        ";",
        optional($._expression),
        ")",
        $.block
      ),

    _for_init: $ => choice(
      $.variable_decl,
      seq($._expression, ";")
    ),

    return_stmt: $ => seq("return", optional($._expression), ";"),

    block: $ => seq("{", repeat($._statement), "}"),

    _expression: $ => choice(
      $.assignment_expr,
      $.binary_expr,
      $.unary_expr,
      $.update_expr, // Postfix ++/--
      $.call_expr,
      $.member_expr,
      $.index_expr,
      $.identifier,
      $.literal,
      $.group_expr,
      $.struct_literal
    ),

    assignment_expr: $ => prec.right(1, seq($._expression, "=", $._expression)),

    logic_expr: $ => prec.left(2, seq($._expression, choice("&&", "||"), $._expression)),
    eq_expr:    $ => prec.left(3, seq($._expression, choice("==", "!="), $._expression)),
    cmp_expr:   $ => prec.left(4, seq($._expression, choice("<", ">", "<=", ">="), $._expression)),
    add_expr:   $ => prec.left(5, seq($._expression, choice("+", "-"), $._expression)),
    mul_expr:   $ => prec.left(6, seq($._expression, choice("*", "/", "%"), $._expression)),

    binary_expr: $ => choice($.mul_expr, $.add_expr, $.cmp_expr, $.eq_expr, $.logic_expr),

    unary_expr: $ => prec.right(8, seq(choice("-", "!", "++", "--"), $._expression)),

    // Added support for postfix operators like i++
    update_expr: $ => prec.left(9, seq($._expression, choice("++", "--"))),

    call_expr: $ => prec(10, seq(
      $._expression,
      "(",
      optional(seq($._expression, repeat(seq(",", $._expression)))),
      ")"
    )),

    member_expr: $ => prec.left(11, seq($._expression, ".", $.identifier)),
    index_expr: $ => prec.left(11, seq($._expression, "[", $._expression, "]")),

    group_expr: $ => seq("(", $._expression, ")"),

    literal: $ => choice($.int, $.float, $.bool, $.string, $.array_literal),
    int: $ => /[0-9]+/,
    float: $ => /[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/,
    bool: $ => choice("true", "false"),
    string: $ => /"([^"]*)"/,

    array_literal: $ =>
      seq("[", optional(seq($._expression, repeat(seq(",", $._expression)))), "]"),

    struct_literal: $ =>
      seq($.identifier, "{", repeat($.struct_init_field), "}"),

    struct_init_field: $ => seq($.identifier, ":", $._expression, optional(",")),

    type: $ => choice(
      "i32", "i64", "f32", "f64", "bool", "str", "string",
      "vec2", "vec3", "vec4", "dvec2", "dvec3", "dvec4", "ivec2", "ivec3", "ivec4",
      $.identifier
    ),

    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    comment: $ => token(choice(
      seq("//", /.*/),
      seq("/*", /[^*]*\*+([^/*][^*]*\*+)*/, "/")
    ))
  }
});
