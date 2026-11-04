/* Builtin string methods (Python-flavoured standard library, v0.5). */
let s = "  Hello, Tesseract!  ";
let t = s.trim();
put(t);
put(t.upper());
put(t.lower());
put(t.contains("Tess"));
put(t.starts_with("Hello"));
put(t.ends_with("!"));
put(t.find("Tess"));
put(t.at(0));
put(t.len());
put(t.replace("l", "L"));
put("ab".repeat(3));
put("abc".reverse());
put("abcdef".substr(1, 4));
put(t.count("e"));
put(t.is_empty());
put("a" + "b".upper());
