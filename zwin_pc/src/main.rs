use std::collections::HashMap;
use std::env;
use std::fs;
use std::io::{self, Write};
use std::process::Command;
use std::thread;
use std::time::Duration;

// ---------------------------------------------------------------------------
// 1. Zwin Value and Block Types
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
enum ZwinValue {
    Number(f64),
    String(String),
}

impl ZwinValue {
    fn to_string(&self) -> String {
        match self {
            ZwinValue::Number(n) => n.to_string(),
            ZwinValue::String(s) => s.clone(),
        }
    }

    fn to_float(&self) -> f64 {
        match self {
            ZwinValue::Number(n) => *n,
            ZwinValue::String(s) => s.parse::<f64>().unwrap_or(0.0),
        }
    }
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum BlockType {
    If,
    Else,
    Loop,
}

#[derive(Clone, Debug)]
struct ZwinBlock {
    block_type: BlockType,
    active: bool,
    loop_start_line: usize,
    loop_count: usize,
}

// Function signature for registered capabilities
type ZwinFunc = fn(params: &str) -> String;

// ---------------------------------------------------------------------------
// 2. VM Struct Definition
// ---------------------------------------------------------------------------

struct ZwinVM {
    variables: HashMap<String, ZwinValue>,
    block_stack: Vec<ZwinBlock>,
    functions: HashMap<String, ZwinFunc>,
}

impl ZwinVM {
    fn new() -> Self {
        ZwinVM {
            variables: HashMap::new(),
            block_stack: Vec::new(),
            functions: HashMap::new(),
        }
    }

    fn register_function(&mut self, name: &str, func: ZwinFunc) {
        self.functions.insert(name.to_string(), func);
    }

    fn set_var_num(&mut self, name: &str, val: f64) {
        self.variables.insert(name.to_string(), ZwinValue::Number(val));
    }

    fn set_var_str(&mut self, name: &str, val: &str) {
        self.variables.insert(name.to_string(), ZwinValue::String(val.to_string()));
    }

    fn get_var_num(&self, name: &str, default: f64) -> f64 {
        if let Some(val) = self.variables.get(name) {
            val.to_float()
        } else {
            default
        }
    }

    fn get_var_str(&self, name: &str, default: &str) -> String {
        if let Some(val) = self.variables.get(name) {
            val.to_string()
        } else {
            default.to_string()
        }
    }

    fn evaluate_expression(&self, expr: &str) -> Result<ZwinValue, String> {
        let trimmed = expr.trim();
        
        // 1. Function Call
        if trimmed.starts_with("call ") {
            let res = self.execute_call(trimmed)?;
            // Attempt to treat return val as numeric if it parses clean
            if let Ok(n) = res.parse::<f64>() {
                return Ok(ZwinValue::Number(n));
            }
            return Ok(ZwinValue::String(res));
        }

        // 2. Quoted String Literal
        if trimmed.starts_with('"') && trimmed.ends_with('"') {
            return Ok(ZwinValue::String(trimmed[1..trimmed.len() - 1].to_string()));
        }

        // 3. Variable Reference
        if let Some(val) = self.variables.get(trimmed) {
            return Ok(val.clone());
        }

        // 4. Raw Numeric Literal
        if let Ok(n) = trimmed.parse::<f64>() {
            return Ok(ZwinValue::Number(n));
        }

        Err(format!("Invalid expression or undefined variable: '{}'", trimmed))
    }

    fn evaluate_condition(&self, cond: &str) -> Result<bool, String> {
        // Supported operators: ==, !=, <=, >=, <, >
        let operators = ["==", "!=", "<=", ">=", "<", ">"];
        let mut found_op = None;
        
        for op in &operators {
            if let Some(idx) = cond.find(op) {
                found_op = Some((*op, idx));
                break;
            }
        }

        if let Some((op, idx)) = found_op {
            let lhs_str = cond[0..idx].trim();
            let rhs_str = cond[idx + op.len()..].trim();
            
            let lhs = self.evaluate_expression(lhs_str)?;
            let rhs = self.evaluate_expression(rhs_str)?;
            
            match (lhs, rhs) {
                (ZwinValue::Number(n1), ZwinValue::Number(n2)) => {
                    match op {
                        "==" => Ok(n1 == n2),
                        "!=" => Ok(n1 != n2),
                        "<"  => Ok(n1 < n2),
                        ">"  => Ok(n1 > n2),
                        "<=" => Ok(n1 <= n2),
                        ">=" => Ok(n1 >= n2),
                        _ => Err(format!("Unknown numeric operator: {}", op)),
                    }
                }
                (val1, val2) => {
                    let s1 = val1.to_string();
                    let s2 = val2.to_string();
                    match op {
                        "==" => Ok(s1 == s2),
                        "!=" => Ok(s1 != s2),
                        _ => Err(format!("Unsupported string comparison operator: {}", op)),
                    }
                }
            }
        } else {
            // Evaluate single expression (e.g. if x)
            let val = self.evaluate_expression(cond)?;
            match val {
                ZwinValue::Number(n) => Ok(n != 0.0),
                ZwinValue::String(s) => Ok(s == "true"),
            }
        }
    }

    fn execute_call(&self, call_str: &str) -> Result<String, String> {
        let call_offset = if call_str.starts_with("call ") { 5 } else { 0 };
        let paren_open = call_str.find('(').ok_or("Missing opening parenthesis '(' in call.")?;
        let paren_close = call_str.rfind(')').ok_or("Missing closing parenthesis ')' in call.")?;

        if paren_close < paren_open {
            return Err("Mismatched parentheses in call statement.".to_string());
        }

        let func_name = call_str[call_offset..paren_open].trim();
        let params = call_str[paren_open + 1..paren_close].trim();

        // Dynamically evaluate parameters: e.g. param=expr
        let mut evaluated_params = String::new();
        let mut param_idx = 0;
        
        while param_idx < params.len() {
            let eq_idx = match params[param_idx..].find('=') {
                Some(idx) => param_idx + idx,
                None => {
                    evaluated_params.push_str(&params[param_idx..]);
                    break;
                }
            };

            evaluated_params.push_str(&params[param_idx..eq_idx + 1]); // Add key and '='

            // Find the next comma that is NOT inside quotes
            let mut comma_idx = None;
            let mut in_quotes = false;
            let bytes = params.as_bytes();
            for i in (eq_idx + 1)..params.len() {
                let c = bytes[i] as char;
                if c == '"' {
                    in_quotes = !in_quotes;
                } else if c == ',' && !in_quotes {
                    comma_idx = Some(i);
                    break;
                }
            }

            let raw_val = match comma_idx {
                Some(idx) => &params[eq_idx + 1..idx],
                None => &params[eq_idx + 1..],
            };
            let raw_val = raw_val.trim();

            let eval_val = self.evaluate_expression(raw_val)?.to_string();
            
            // Format evaluation back to capability call format (quotes if string/chars)
            let is_numeric = eval_val.parse::<f64>().is_ok();
            if is_numeric {
                evaluated_params.push_str(&eval_val);
            } else {
                evaluated_params.push_str(&format!("\"{}\"", eval_val));
            }

            if let Some(idx) = comma_idx {
                evaluated_params.push_str(", ");
                param_idx = idx + 1;
            } else {
                break;
            }
        }

        if let Some(func) = self.functions.get(func_name) {
            Ok(func(&evaluated_params))
        } else {
            Err(format!("Call failed: function '{}' is not registered.", func_name))
        }
    }

    fn execute(&mut self, script: &str) -> Result<(), String> {
        let lines: Vec<&str> = script.lines().collect();
        let line_count = lines.len();
        self.block_stack.clear();

        let mut pc = 0;
        let mut instruction_count = 0;
        let instruction_limit = 5000;
        let mut last_if_evaluated_true = false;

        while pc < line_count {
            instruction_count += 1;
            if instruction_count > instruction_limit {
                return Err("Runtime error: Instruction limit exceeded (possible infinite loop).".to_string());
            }

            let mut line = lines[pc].trim();
            
            // Skip comments and empty lines
            if line.is_empty() || line.starts_with('#') {
                pc += 1;
                continue;
            }

            // Inline trailing comment strip
            if let Some(comment_idx) = line.find('#') {
                line = line[0..comment_idx].trim();
            }

            // Handle block close
            if line == "}" {
                if self.block_stack.is_empty() {
                    return Err(format!("Syntax error on line {}: Unmatched closing brace '}}'.", pc + 1));
                }
                
                let block = self.block_stack.last().unwrap().clone();
                if block.block_type == BlockType::Loop && block.active && block.loop_count > 0 {
                    self.block_stack.last_mut().unwrap().loop_count -= 1;
                    pc = block.loop_start_line;
                } else {
                    self.block_stack.pop();
                    pc += 1;
                }
                continue;
            }

            // Determine execution activation state based on stack parent scopes
            let mut should_execute = true;
            for block in &self.block_stack {
                if !block.active {
                    should_execute = false;
                    break;
                }
            }

            // Handle conditional header scopes
            if line.starts_with("if ") && line.ends_with('{') {
                let mut cond_result = false;
                if should_execute {
                    let cond_str = line[3..line.len() - 1].trim();
                    cond_result = self.evaluate_condition(cond_str)
                        .map_err(|e| format!("Line {}: {}", pc + 1, e))?;
                    last_if_evaluated_true = cond_result;
                }
                self.block_stack.push(ZwinBlock {
                    block_type: BlockType::If,
                    active: should_execute && cond_result,
                    loop_start_line: 0,
                    loop_count: 0,
                });
                pc += 1;
                continue;
            } else if (line.starts_with("else {") || line.starts_with("else{")) && line.ends_with('{') {
                self.block_stack.push(ZwinBlock {
                    block_type: BlockType::Else,
                    active: should_execute && !last_if_evaluated_true,
                    loop_start_line: 0,
                    loop_count: 0,
                });
                pc += 1;
                continue;
            } else if line.starts_with("loop ") && line.ends_with('{') {
                let mut count = 0;
                if should_execute {
                    let count_str = line[5..line.len() - 1].trim();
                    let val = self.evaluate_expression(count_str)
                        .map_err(|e| format!("Line {}: {}", pc + 1, e))?;
                    count = val.to_float() as usize;
                }
                self.block_stack.push(ZwinBlock {
                    block_type: BlockType::Loop,
                    active: should_execute && (count > 0),
                    loop_start_line: pc + 1,
                    loop_count: if count > 0 { count - 1 } else { 0 },
                });
                pc += 1;
                continue;
            }

            if !should_execute {
                pc += 1;
                continue;
            }

            // Process statements
            if line.starts_with("var ") {
                let eq_idx = line.find('=').ok_or(format!("Syntax error on line {}: Missing '=' in variable declaration.", pc + 1))?;
                let var_name = line[4..eq_idx].trim().to_string();
                let expr = line[eq_idx + 1..].trim();
                
                let val = self.evaluate_expression(expr)
                    .map_err(|e| format!("Line {}: {}", pc + 1, e))?;
                self.variables.insert(var_name, val);
            } else if line.starts_with("delay(") {
                let end_paren = line.find(')').ok_or(format!("Syntax error on line {}: Missing ')' in delay call.", pc + 1))?;
                let delay_expr = line[6..end_paren].trim();
                let val = self.evaluate_expression(delay_expr)
                    .map_err(|e| format!("Line {}: {}", pc + 1, e))?;
                let ms = val.to_float() as u64;
                if ms > 0 {
                    thread::sleep(Duration::from_millis(ms));
                }
            } else if line.starts_with("call ") {
                self.execute_call(line)
                    .map_err(|e| format!("Line {}: {}", pc + 1, e))?;
            } else {
                return Err(format!("Syntax error on line {}: Unknown statement: '{}'", pc + 1, line));
            }

            pc += 1;
        }

        if !self.block_stack.is_empty() {
            return Err("Syntax error: Missing closing brace '}' at end of script.".to_string());
        }

        Ok(())
    }
}

// ---------------------------------------------------------------------------
// 3. PC SDK Binding Helpers
// ---------------------------------------------------------------------------

fn get_param_value(params: &str, key: &str) -> String {
    let pattern = format!("{}=", key);
    if let Some(idx) = params.find(&pattern) {
        let start = idx + pattern.len();
        let mut end = start;
        let bytes = params.as_bytes();
        let mut in_quotes = false;
        while end < params.len() {
            let c = bytes[end] as char;
            if c == '"' {
                in_quotes = !in_quotes;
            } else if c == ',' && !in_quotes {
                break;
            }
            end += 1;
        }
        let mut val = params[start..end].trim().to_string();
        if val.starts_with('"') && val.ends_with('"') {
            val = val[1..val.len() - 1].to_string();
        }
        val
    } else {
        "".to_string()
    }
}

// ---------------------------------------------------------------------------
// 4. Concrete PC Capability Implementations
// ---------------------------------------------------------------------------

fn cb_io_print(params: &str) -> String {
    let text = get_param_value(params, "text");
    println!("{}", text);
    "1".to_string()
}

fn cb_io_read(_params: &str) -> String {
    print!("> ");
    let _ = io::stdout().flush();
    let mut input = String::new();
    io::stdin().read_line(&mut input).unwrap_or(0);
    input.trim().to_string()
}

fn cb_file_write(params: &str) -> String {
    let filename = get_param_value(params, "filename");
    let content = get_param_value(params, "content");
    
    if filename.is_empty() {
        return "Error: Missing filename parameter.".to_string();
    }
    
    match fs::write(&filename, content) {
        Ok(_) => "1".to_string(),
        Err(e) => format!("Error writing file: {}", e),
    }
}

fn cb_file_read(params: &str) -> String {
    let filename = get_param_value(params, "filename");
    if filename.is_empty() {
        return "Error: Missing filename parameter.".to_string();
    }
    
    match fs::read_to_string(&filename) {
        Ok(content) => content,
        Err(e) => format!("Error reading file: {}", e),
    }
}

fn cb_file_delete(params: &str) -> String {
    let filename = get_param_value(params, "filename");
    if filename.is_empty() {
        return "Error: Missing filename parameter.".to_string();
    }
    
    match fs::remove_file(&filename) {
        Ok(_) => "1".to_string(),
        Err(e) => format!("Error deleting file: {}", e),
    }
}

fn cb_file_exists(params: &str) -> String {
    let filename = get_param_value(params, "filename");
    if filename.is_empty() {
        return "0".to_string();
    }
    if fs::metadata(filename).is_ok() {
        "1".to_string()
    } else {
        "0".to_string()
    }
}

fn cb_system_execute(params: &str) -> String {
    let command = get_param_value(params, "command");
    if command.is_empty() {
        return "Error: Missing command parameter.".to_string();
    }
    
    let output = if cfg!(target_os = "windows") {
        Command::new("powershell")
            .args(&["-Command", &command])
            .output()
    } else {
        Command::new("sh")
            .args(&["-c", &command])
            .output()
    };
    
    match output {
        Ok(out) => {
            let stdout = String::from_utf8_lossy(&out.stdout).to_string();
            let stderr = String::from_utf8_lossy(&out.stderr).to_string();
            if out.status.success() {
                stdout
            } else {
                format!("Error details:\nStdout: {}\nStderr: {}", stdout, stderr)
            }
        }
        Err(e) => format!("Failed to run subprocess command: {}", e),
    }
}

fn cb_llm_chat(params: &str) -> String {
    let prompt = get_param_value(params, "prompt");
    if prompt.is_empty() {
        return "Error: Missing prompt parameter.".to_string();
    }
    
    let api_key = env::var("GEMINI_API_KEY").unwrap_or_default();
    if api_key.is_empty() {
        return "Error: GEMINI_API_KEY environment variable is not set.".to_string();
    }
    
    let url = format!(
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={}",
        api_key
    );
    
    let client = reqwest::blocking::Client::new();
    let body = serde_json::json!({
        "contents": [{
            "parts": [{
                "text": prompt
            }]
        }]
    });
    
    match client.post(&url).json(&body).send() {
        Ok(resp) => {
            if resp.status().is_success() {
                if let Ok(res_json) = resp.json::<serde_json::Value>() {
                    if let Some(text) = res_json["candidates"][0]["content"]["parts"][0]["text"].as_str() {
                        return text.to_string();
                    }
                }
                "Error: Failed to parse Gemini response JSON.".to_string()
            } else {
                format!("Error: Gemini API returned status {}.", resp.status())
            }
        }
        Err(e) => format!("Error: Failed to connect to Gemini API. {}", e),
    }
}

fn cb_telegram_send(params: &str) -> String {
    let token = get_param_value(params, "token");
    let chat_id = get_param_value(params, "chat_id");
    let text = get_param_value(params, "text");
    
    if token.is_empty() || chat_id.is_empty() || text.is_empty() {
        return "Error: Missing token, chat_id, or text parameter.".to_string();
    }
    
    let url = format!("https://api.telegram.org/bot{}/sendMessage", token);
    let client = reqwest::blocking::Client::new();
    let body = serde_json::json!({
        "chat_id": chat_id,
        "text": text
    });
    
    match client.post(&url).json(&body).send() {
        Ok(resp) => {
            if resp.status().is_success() {
                "Success".to_string()
            } else {
                format!("Error: Telegram API returned status {}", resp.status())
            }
        }
        Err(e) => format!("Error: Failed to connect to Telegram API. {}", e),
    }
}

fn cb_telegram_poll(params: &str) -> String {
    let token = get_param_value(params, "token");
    let offset = get_param_value(params, "offset");
    
    if token.is_empty() {
        return "Error: Missing token parameter.".to_string();
    }
    
    let mut url = format!("https://api.telegram.org/bot{}/getUpdates?timeout=1", token);
    if !offset.is_empty() {
        url.push_str(&format!("&offset={}", offset));
    }
    
    let client = reqwest::blocking::Client::new();
    match client.get(&url).send() {
        Ok(resp) => {
            if resp.status().is_success() {
                resp.text().unwrap_or_default()
            } else {
                format!("Error: Telegram API returned status {}", resp.status())
            }
        }
        Err(e) => format!("Error: Failed to connect to Telegram API. {}", e),
    }
}

// ---------------------------------------------------------------------------
// 5. Main CLI Entry Point
// ---------------------------------------------------------------------------

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        println!("⚡ Zwin VM PC Runner v0.1");
        println!("Usage: zwin <script.zwin>");
        return;
    }

    let filename = &args[1];
    let script = match fs::read_to_string(filename) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading script file '{}': {}", filename, e);
            std::process::exit(1);
        }
    };

    let mut vm = ZwinVM::new();
    
    // Register PC native SDK functions
    vm.register_function("io.print", cb_io_print);
    vm.register_function("io.read", cb_io_read);
    vm.register_function("file.write", cb_file_write);
    vm.register_function("file.read", cb_file_read);
    vm.register_function("file.delete", cb_file_delete);
    vm.register_function("file.exists", cb_file_exists);
    vm.register_function("system.execute", cb_system_execute);
    vm.register_function("llm.chat", cb_llm_chat);
    vm.register_function("telegram.send", cb_telegram_send);
    vm.register_function("telegram.poll", cb_telegram_poll);

    match vm.execute(&script) {
        Ok(_) => {}
        Err(e) => {
            eprintln!("Zwin VM Error: {}", e);
            std::process::exit(1);
        }
    }
}
