package multilang

import "testing"

func TestValidLanguageIDs(t *testing.T) {
	cases := []struct{ tag, want string }{
		{"en", "en"},
		{"es", "es"},
		{"pt-BR", "pt-br"},
		{"zh-Hans", "zh-hans"},
		{"zh-Hans-CN", "zh-hans-cn"},
		{"en-US", "en-us"},
		{"sr-Latn-RS", "sr-latn-rs"},
	}
	for _, c := range cases {
		got, err := ValidateLanguageID(c.tag)
		if err != nil {
			t.Fatalf("ValidateLanguageID(%q) unexpected error: %v", c.tag, err)
		}
		if got != c.want {
			t.Errorf("ValidateLanguageID(%q) = %q, want %q", c.tag, got, c.want)
		}
	}
}

func TestInvalidLanguageIDs(t *testing.T) {
	for _, tag := range []string{"", "english", "e", "en_US", "en--US", "123"} {
		if _, err := ValidateLanguageID(tag); err == nil {
			t.Errorf("ValidateLanguageID(%q) expected error, got nil", tag)
		}
	}
}

func TestOptionalLanguageIDEmptyIsEmpty(t *testing.T) {
	got, err := ValidateOptionalLanguageID("")
	if err != nil || got != "" {
		t.Errorf("ValidateOptionalLanguageID(\"\") = (%q, %v), want (\"\", nil)", got, err)
	}
}

func TestValidStringIDs(t *testing.T) {
	long := make([]byte, 200)
	for i := range long {
		long[i] = 'a'
	}
	for _, sid := range []string{"hello", "button.publish", "menu:item-42", string(long)} {
		got, err := ValidateStringID(sid)
		if err != nil {
			t.Fatalf("ValidateStringID(%q) unexpected error: %v", sid, err)
		}
		if got != sid {
			t.Errorf("ValidateStringID(%q) = %q, want unchanged", sid, got)
		}
	}
}

func TestInvalidStringIDs(t *testing.T) {
	long := make([]byte, 201)
	for i := range long {
		long[i] = 'a'
	}
	for _, sid := range []string{"", string(long), "has space", "has/slash", "quote'"} {
		if _, err := ValidateStringID(sid); err == nil {
			t.Errorf("ValidateStringID(%q) expected error, got nil", sid)
		}
	}
}

func TestStringIDLowercased(t *testing.T) {
	got, err := ValidateStringID("Button.Publish")
	if err != nil || got != "button.publish" {
		t.Errorf("ValidateStringID lowercasing failed: got (%q, %v)", got, err)
	}
}

func TestContextDefaultsToEmptyString(t *testing.T) {
	got, err := ValidateContext("")
	if err != nil || got != "" {
		t.Errorf("ValidateContext(\"\") = (%q, %v), want (\"\", nil)", got, err)
	}
}

func TestContextLowercased(t *testing.T) {
	got, err := ValidateContext("Menu.Item")
	if err != nil || got != "menu.item" {
		t.Errorf("ValidateContext lowercasing failed: got (%q, %v)", got, err)
	}
}

func TestContentRejectsNulByte(t *testing.T) {
	if _, err := ValidateContent("hello\x00world"); err == nil {
		t.Error("expected error for NUL byte content")
	}
}

func TestContentRejectsEmpty(t *testing.T) {
	if _, err := ValidateContent(""); err == nil {
		t.Error("expected error for empty content")
	}
}

func TestStatusAllowlist(t *testing.T) {
	for _, ok := range []string{"draft", "reviewed", "published"} {
		got, err := ValidateStatus(ok)
		if err != nil || got != ok {
			t.Errorf("ValidateStatus(%q) = (%q, %v), want (%q, nil)", ok, got, err, ok)
		}
	}
	if _, err := ValidateStatus("live"); err == nil {
		t.Error("expected error for invalid status")
	}
}
