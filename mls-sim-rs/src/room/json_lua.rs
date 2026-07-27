use mlua::prelude::*;
use serde_json::Value as Jv;
use std::collections::HashSet;
use std::ffi::c_void;

const MAX_ENCODE_DEPTH: usize = 128;

pub fn install_json_lib(lua: &Lua) -> LuaResult<()> {
    let json = lua.create_table()?;
    json.set("encode", lua.create_function(json_encode)?)?;
    json.set("decode", lua.create_function(json_decode)?)?;
    lua.globals().set("json", json)?;
    Ok(())
}

fn json_encode(lua: &Lua, value: LuaValue) -> LuaResult<LuaValue> {
    let mut buf = Vec::new();
    let mut active_tables = HashSet::new();
    encode_value(&value, &mut buf, &mut active_tables, 0)?;
    Ok(LuaValue::String(lua.create_string(&buf)?))
}

fn json_decode(lua: &Lua, value: LuaValue) -> LuaResult<LuaValue> {
    let s = match value {
        LuaValue::Nil => return Ok(LuaValue::Nil),
        LuaValue::String(ref s) => match s.to_str() {
            Ok(s) if s.is_empty() => return Ok(LuaValue::Nil),
            Ok(s) => s.to_owned(),
            Err(e) => return Err(LuaError::RuntimeError(format!("json.decode: {e}"))),
        },
        _ => {
            return Err(LuaError::RuntimeError(
                "json.decode expects a string".into(),
            ))
        }
    };
    let v: Jv = serde_json::from_str(&s)
        .map_err(|e| LuaError::RuntimeError(format!("json.decode: {e}")))?;
    json_to_lua(lua, &v)
}

// ── encode: build JSON as raw bytes (matching cjson byte-pass-through) ──

fn encode_value(
    v: &LuaValue,
    buf: &mut Vec<u8>,
    active_tables: &mut HashSet<*const c_void>,
    depth: usize,
) -> LuaResult<()> {
    match v {
        LuaValue::Nil => buf.extend_from_slice(b"null"),
        LuaValue::Boolean(b) => {
            buf.extend_from_slice(if *b { b"true" } else { b"false" });
        }
        LuaValue::Integer(i) => {
            let s = i.to_string();
            buf.extend_from_slice(s.as_bytes());
        }
        LuaValue::Number(n) => encode_number(*n, buf),
        LuaValue::String(s) => encode_lua_string(s, buf),
        LuaValue::Table(t) => encode_table(t, buf, active_tables, depth)?,
        _ => buf.extend_from_slice(b"null"),
    }
    Ok(())
}

fn encode_number(n: f64, buf: &mut Vec<u8>) {
    if n.is_nan() || n.is_infinite() {
        buf.extend_from_slice(b"null");
        return;
    }
    if n == n.floor() && n.abs() < 1e15 {
        let s = format!("{}", n as i64);
        buf.extend_from_slice(s.as_bytes());
    } else {
        let s = format!("{:.14e}", n);
        let trimmed = trim_float(&s);
        buf.extend_from_slice(trimmed.as_bytes());
    }
}

fn trim_float(s: &str) -> String {
    if let Some(dot) = s.find('.') {
        if let Some(e_pos) = s[dot..].find(|c: char| c == 'e' || c == 'E') {
            let mantissa = &s[..dot + e_pos];
            let exp = &s[dot + e_pos..];
            let trimmed = mantissa.trim_end_matches('0');
            let trimmed = if trimmed.ends_with('.') {
                &trimmed[..trimmed.len() - 1]
            } else {
                trimmed
            };
            if exp == "e0" || exp == "E0" {
                return trimmed.to_string();
            }
            return format!("{}{}", trimmed, exp);
        }
    }
    s.to_string()
}

/// Write a Lua string as a JSON string value, passing raw bytes through
/// (only escaping characters required by JSON spec). This matches cjson behavior.
fn encode_lua_string(s: &mlua::String, buf: &mut Vec<u8>) {
    let bytes = s.as_bytes();
    buf.push(b'"');
    for &b in bytes.iter() {
        match b {
            b'\\' => buf.extend_from_slice(b"\\\\"),
            b'"' => buf.extend_from_slice(b"\\\""),
            b'\n' => buf.extend_from_slice(b"\\n"),
            b'\r' => buf.extend_from_slice(b"\\r"),
            b'\t' => buf.extend_from_slice(b"\\t"),
            0x00..=0x1f => {
                let esc = format!("\\u{:04x}", b);
                buf.extend_from_slice(esc.as_bytes());
            }
            _ => buf.push(b),
        }
    }
    buf.push(b'"');
}

// cjson: empty table → object, sequential 1..n → array, otherwise → object
fn is_array_table(t: &LuaTable) -> bool {
    let mut n = 0i64;
    for pair in t.pairs::<LuaValue, LuaValue>() {
        if pair.is_ok() {
            n += 1;
        }
    }
    if n == 0 {
        return false;
    }
    for i in 1..=n {
        match t.raw_get::<LuaValue>(i) {
            Ok(LuaValue::Nil) | Err(_) => return false,
            _ => {}
        }
    }
    true
}

fn encode_table(
    t: &LuaTable,
    buf: &mut Vec<u8>,
    active_tables: &mut HashSet<*const c_void>,
    depth: usize,
) -> LuaResult<()> {
    if depth >= MAX_ENCODE_DEPTH {
        return Err(LuaError::RuntimeError(format!(
            "json.encode: nesting depth exceeds {MAX_ENCODE_DEPTH}"
        )));
    }

    let table_pointer = t.to_pointer();
    if !active_tables.insert(table_pointer) {
        return Err(LuaError::RuntimeError(
            "json.encode: circular reference".into(),
        ));
    }

    let result = (|| {
        if is_array_table(t) {
            buf.push(b'[');
            let len = t.raw_len();
            for i in 1..=len {
                if i > 1 {
                    buf.push(b',');
                }
                let v: LuaValue = t.raw_get(i as i64).unwrap_or(LuaValue::Nil);
                encode_value(&v, buf, active_tables, depth + 1)?;
            }
            buf.push(b']');
        } else {
            buf.push(b'{');
            let mut first = true;
            for pair in t.pairs::<LuaValue, LuaValue>() {
                if let Ok((k, v)) = pair {
                    if !first {
                        buf.push(b',');
                    }
                    first = false;
                    encode_key(&k, buf);
                    buf.push(b':');
                    encode_value(&v, buf, active_tables, depth + 1)?;
                }
            }
            buf.push(b'}');
        }
        Ok(())
    })();

    active_tables.remove(&table_pointer);
    result
}

fn encode_key(k: &LuaValue, buf: &mut Vec<u8>) {
    match k {
        LuaValue::String(s) => encode_lua_string(s, buf),
        LuaValue::Integer(i) => {
            let s = i.to_string();
            buf.push(b'"');
            buf.extend_from_slice(s.as_bytes());
            buf.push(b'"');
        }
        LuaValue::Number(n) if *n == n.floor() && n.abs() < 1e15 => {
            let s = format!("{}", *n as i64);
            buf.push(b'"');
            buf.extend_from_slice(s.as_bytes());
            buf.push(b'"');
        }
        LuaValue::Number(n) => {
            let s = format!("{n}");
            buf.push(b'"');
            buf.extend_from_slice(s.as_bytes());
            buf.push(b'"');
        }
        _ => buf.extend_from_slice(b"\"\""),
    }
}

// ── decode: still uses serde_json (input is always valid JSON/UTF-8) ──

fn json_to_lua(lua: &Lua, v: &Jv) -> LuaResult<LuaValue> {
    Ok(match v {
        Jv::Null => LuaValue::Nil,
        Jv::Bool(b) => LuaValue::Boolean(*b),
        Jv::Number(n) => {
            if let Some(i) = n.as_i64() {
                LuaValue::Integer(i)
            } else {
                LuaValue::Number(n.as_f64().unwrap_or(0.0))
            }
        }
        Jv::String(s) => LuaValue::String(lua.create_string(s)?),
        Jv::Array(arr) => {
            let t = lua.create_table()?;
            for (i, v) in arr.iter().enumerate() {
                t.raw_set(i as i64 + 1, json_to_lua(lua, v)?)?;
            }
            LuaValue::Table(t)
        }
        Jv::Object(map) => {
            let t = lua.create_table()?;
            for (k, v) in map {
                t.raw_set(lua.create_string(k.as_str())?, json_to_lua(lua, v)?)?;
            }
            LuaValue::Table(t)
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn json_encode_rejects_circular_tables() {
        let lua = Lua::new();
        install_json_lib(&lua).unwrap();

        let (success, message): (bool, String) = lua
            .load(
                r#"
local value = {}
value.self = value
local ok, err = pcall(json.encode, value)
return ok, tostring(err)
"#,
            )
            .eval()
            .unwrap();

        assert!(!success);
        assert!(message.contains("circular reference"), "{message}");
    }

    #[test]
    fn json_encode_rejects_excessive_nesting() {
        let lua = Lua::new();
        install_json_lib(&lua).unwrap();

        let (success, message): (bool, String) = lua
            .load(
                r#"
local root = {}
local current = root
for _ = 1, 129 do
    local child = {}
    current.child = child
    current = child
end
local ok, err = pcall(json.encode, root)
return ok, tostring(err)
"#,
            )
            .eval()
            .unwrap();

        assert!(!success);
        assert!(message.contains("nesting depth"), "{message}");
    }

    #[test]
    fn json_encode_allows_shared_non_circular_tables() {
        let lua = Lua::new();
        install_json_lib(&lua).unwrap();

        let (first, second): (i64, i64) = lua
            .load(
                r#"
local child = { value = 7 }
local decoded = json.decode(json.encode({ first = child, second = child }))
return decoded.first.value, decoded.second.value
"#,
            )
            .eval()
            .unwrap();

        assert_eq!((first, second), (7, 7));
    }
}
