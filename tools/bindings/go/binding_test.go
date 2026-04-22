package tree_sitter_hydrolox_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_hydrolox "github.com/themasterofworlds99/hydrolox/tree/main/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_hydrolox.Language())
	if language == nil {
		t.Errorf("Error loading Hydrolox grammar")
	}
}
