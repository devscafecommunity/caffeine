//! Editor assistance for scripts, with no language server: symbols defined in the file,
//! completion candidates and go-to-definition.

use crate::uikit::script_reference::{Entry, Language, ENTRIES};

/// A function or variable declared in the open file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Definition {
    pub name: String,
    /// `function name(a, b)` or `local name` as written.
    pub signature: String,
    pub line: usize,
    /// Column of the name.
    pub column: usize,
    pub is_function: bool,
}

fn is_word(c: char) -> bool {
    c.is_alphanumeric() || c == '_'
}

/// Declarations found by reading the text line by line.
pub fn definitions(language: Language, text: &str) -> Vec<Definition> {
    let mut found = Vec::new();
    for (line_index, line) in text.lines().enumerate() {
        let trimmed = line.trim_start();
        let indent = line.len() - trimmed.len();
        let mut rest = trimmed;
        let mut offset = indent;
        let mut is_function = false;
        let mut matched = false;
        let prefixes: &[&str] = match language {
            Language::Lua => &["local function ", "function ", "local "],
            Language::Dryad => &["function ", "let "],
        };
        for prefix in prefixes {
            if let Some(after) = rest.strip_prefix(prefix) {
                offset += prefix.len();
                rest = after;
                is_function = prefix.contains("function");
                matched = true;
                break;
            }
        }
        if !matched {
            continue;
        }
        let name: String = rest
            .chars()
            .take_while(|c| is_word(*c) || (*c == '.' && is_function))
            .collect();
        if name.is_empty() || name.chars().next().is_some_and(|c| c.is_ascii_digit()) {
            continue;
        }
        // `local a, b = ...` declares several names; the first is enough for navigation.
        let signature = if is_function {
            let end = rest.find(')').map_or(rest.len(), |i| i + 1);
            format!("function {}", &rest[..end])
        } else {
            format!(
                "{} {}",
                if language == Language::Lua {
                    "local"
                } else {
                    "let"
                },
                name
            )
        };
        let column = line[..offset].chars().count();
        found.push(Definition {
            name,
            signature,
            line: line_index,
            column,
            is_function,
        });
    }
    found
}

/// The definition a bare name refers to (the last one above `before_line`, else the first).
pub fn find_definition<'a>(
    definitions: &'a [Definition],
    name: &str,
    before_line: usize,
) -> Option<&'a Definition> {
    definitions
        .iter()
        .rfind(|d| d.name == name && d.line <= before_line)
        .or_else(|| definitions.iter().find(|d| d.name == name))
}

/// Completion offered at the cursor: the typed prefix and the candidates.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Completion {
    /// Column where the typed prefix starts.
    pub start: usize,
    pub items: Vec<String>,
}

/// Words and API names that continue what is typed before `column` on `line`.
pub fn completions(
    language: Language,
    text: &str,
    line_index: usize,
    column: usize,
) -> Option<Completion> {
    let line = text.lines().nth(line_index)?;
    let chars: Vec<char> = line.chars().collect();
    let column = column.min(chars.len());
    let mut start = column;
    while start > 0 && (is_word(chars[start - 1]) || chars[start - 1] == '.') {
        start -= 1;
    }
    let prefix: String = chars[start..column].iter().collect();
    if prefix.is_empty() || prefix.ends_with(' ') {
        return None;
    }
    // Two letters, or a dotted path, before suggesting anything.
    if prefix.chars().count() < 2 && !prefix.contains('.') {
        return None;
    }
    let mut items: Vec<String> = ENTRIES
        .iter()
        .filter(|entry: &&Entry| entry.language == language)
        .map(|entry| entry.name.to_owned())
        .chain(
            definitions(language, text)
                .into_iter()
                .map(|definition| definition.name),
        )
        .filter(|name| name.starts_with(&prefix) && *name != prefix)
        .collect();
    items.sort();
    items.dedup();
    items.truncate(40);
    (!items.is_empty()).then_some(Completion { start, items })
}

#[cfg(test)]
mod tests {
    use super::*;

    const LUA: &str = "local speed = 4\nfunction onUpdate(entityId, dt)\n  local function helper(a, b) end\nend\n";

    #[test]
    fn finds_functions_and_locals() {
        let defs = definitions(Language::Lua, LUA);
        let names: Vec<&str> = defs.iter().map(|d| d.name.as_str()).collect();
        assert_eq!(names, ["speed", "onUpdate", "helper"]);
        assert!(defs[1].is_function);
        assert_eq!(defs[1].signature, "function onUpdate(entityId, dt)");
        assert_eq!(defs[2].line, 2);
        assert_eq!(defs[2].column, 17);
        let dyd = definitions(
            Language::Dryad,
            "let x = 1;\nfunction onUpdate(entity, dt) {\n}\n",
        );
        assert_eq!(dyd.len(), 2);
    }

    #[test]
    fn go_to_definition_prefers_the_nearest_earlier_one() {
        let text = "local a = 1\nlocal a = 2\nprint(a)\n";
        let defs = definitions(Language::Lua, text);
        assert_eq!(find_definition(&defs, "a", 2).map(|d| d.line), Some(1));
        assert!(find_definition(&defs, "missing", 2).is_none());
    }

    #[test]
    fn completes_api_names_keywords_and_locals() {
        let text = "local speedy = 1\nspe";
        let found = completions(Language::Lua, text, 1, 3).expect("completion");
        assert_eq!(found.start, 0);
        assert_eq!(found.items, ["speedy"]);

        let api = completions(Language::Lua, "caffeine.time.d", 0, 15).expect("api");
        assert!(api.items.iter().any(|i| i == "caffeine.time.delta"));
        assert!(
            completions(Language::Lua, "x", 0, 1).is_none(),
            "one letter is too little"
        );
        assert!(completions(Language::Lua, "caffeine.time.delta", 0, 19).is_none());

        let dyd = completions(Language::Dryad, "caffeine_input_", 0, 15).expect("dryad");
        assert!(dyd.items.iter().all(|i| i.starts_with("caffeine_input_")));
    }
}
