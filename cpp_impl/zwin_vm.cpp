#include "zwin_vm.h"
#include <vector>

ZwinVM::ZwinVM() {}

void ZwinVM::begin() {
    _varCount = 0;
    _blockDepth = 0;
    _funcCount = 0;
}

void ZwinVM::registerFunction(const String& name, ZwinFunc func) {
    if (_funcCount < MAX_FUNCTIONS) {
        _registeredFuncs[_funcCount++] = {name, func};
    }
}

int ZwinVM::findVariable(const String& name) {
    for (int i = 0; i < _varCount; ++i) {
        if (_variables[i].name == name) return i;
    }
    return -1;
}

ZwinFunc ZwinVM::findFunction(const String& name) {
    for (int i = 0; i < _funcCount; ++i) {
        if (_registeredFuncs[i].name == name) return _registeredFuncs[i].func;
    }
    return nullptr;
}

void ZwinVM::setVar(const String& name, float val) {
    int idx = findVariable(name);
    if (idx >= 0) {
        _variables[idx].numVal = val;
        _variables[idx].isNum = true;
        _variables[idx].strVal = String(val);
    } else if (_varCount < MAX_VARIABLES) {
        _variables[_varCount++] = ZwinVar(name, "", val, true);
    }
}

void ZwinVM::setVar(const String& name, const String& val) {
    int idx = findVariable(name);
    if (idx >= 0) {
        _variables[idx].strVal = val;
        _variables[idx].isNum = false;
        _variables[idx].numVal = val.toFloat();
    } else if (_varCount < MAX_VARIABLES) {
        _variables[_varCount++] = ZwinVar(name, val, val.toFloat(), false);
    }
}

float ZwinVM::getVarNum(const String& name, float defaultVal) {
    int idx = findVariable(name);
    if (idx >= 0) return _variables[idx].numVal;
    return defaultVal;
}

String ZwinVM::getVarStr(const String& name, const String& defaultVal) {
    int idx = findVariable(name);
    if (idx >= 0) return _variables[idx].strVal;
    return defaultVal;
}

// Split string helper
static std::vector<String> splitLines(const String& s) {
    std::vector<String> lines;
    int start = 0;
    int end = s.indexOf('\n');
    while (end >= 0) {
        lines.push_back(s.substring(start, end));
        start = end + 1;
        end = s.indexOf('\n', start);
    }
    if (start < s.length()) {
        lines.push_back(s.substring(start));
    }
    return lines;
}

bool ZwinVM::execute(const String& script, String& errorOut) {
    std::vector<String> lines = splitLines(script);
    _blockDepth = 0;
    
    int pc = 0;
    int lineCount = lines.size();
    int instructionCount = 0;
    const int INSTRUCTION_LIMIT = 5000;

    bool lastIfEvaluatedTrue = false; // Used to track 'else' execution

    while (pc < lineCount) {
        instructionCount++;
        if (instructionCount > INSTRUCTION_LIMIT) {
            errorOut = "Runtime error: Instruction limit exceeded (possible infinite loop).";
            return false;
        }

        String line = lines[pc];
        line.trim();

        // 1. Skip comments and empty lines
        if (line.length() == 0 || line.startsWith("#")) {
            pc++;
            continue;
        }

        // 2. Handle block close '}'
        if (line == "}") {
            if (_blockDepth == 0) {
                errorOut = "Syntax error on line " + String(pc + 1) + ": Unmatched closing brace '}'.";
                return false;
            }
            
            ZwinBlock block = _blockStack[_blockDepth - 1];
            if (block.type == ZwinBlock::BLOCK_LOOP && block.active && block.loopCount > 0) {
                // Loop jump back
                _blockStack[_blockDepth - 1].loopCount--;
                pc = block.loopStartLine;
            } else {
                // Pop block
                _blockDepth--;
                pc++;
            }
            continue;
        }

        // 3. Determine if current line should execute (checking parent blocks)
        bool shouldExecute = true;
        for (int i = 0; i < _blockDepth; ++i) {
            if (!_blockStack[i].active) {
                shouldExecute = false;
                break;
            }
        }

        // 4. Handle block headers even if inactive (to balance braces)
        if (line.startsWith("if ") && line.endsWith("{")) {
            bool condResult = false;
            if (shouldExecute) {
                String condStr = line.substring(3, line.length() - 1);
                condStr.trim();
                bool evalOk = false;
                condResult = evaluateCondition(condStr, evalOk, errorOut);
                if (!evalOk) {
                    errorOut = "Line " + String(pc + 1) + ": " + errorOut;
                    return false;
                }
                lastIfEvaluatedTrue = condResult;
            }
            if (_blockDepth >= MAX_BLOCK_DEPTH) {
                errorOut = "Line " + String(pc + 1) + ": Max block nesting depth exceeded.";
                return false;
            }
            _blockStack[_blockDepth++] = ZwinBlock(ZwinBlock::BLOCK_IF, shouldExecute && condResult, -1, 0);
            pc++;
            continue;
        }
        else if (line.startsWith("else {") || line.startsWith("else{")) {
            if (_blockDepth >= MAX_BLOCK_DEPTH) {
                errorOut = "Line " + String(pc + 1) + ": Max block nesting depth exceeded.";
                return false;
            }
            _blockStack[_blockDepth++] = ZwinBlock(ZwinBlock::BLOCK_ELSE, shouldExecute && !lastIfEvaluatedTrue, -1, 0);
            pc++;
            continue;
        }
        else if (line.startsWith("loop ") && line.endsWith("{")) {
            int count = 0;
            if (shouldExecute) {
                String countStr = line.substring(5, line.length() - 1);
                countStr.trim();
                bool evalOk = false;
                String res = evaluateExpression(countStr, evalOk, errorOut);
                if (!evalOk) return false;
                count = res.toInt();
            }
            if (_blockDepth >= MAX_BLOCK_DEPTH) {
                errorOut = "Line " + String(pc + 1) + ": Max block nesting depth exceeded.";
                return false;
            }
            _blockStack[_blockDepth++] = ZwinBlock(ZwinBlock::BLOCK_LOOP, shouldExecute && (count > 0), pc + 1, count - 1);
            pc++;
            continue;
        }

        // If block logic demands skipping, move to the next line
        if (!shouldExecute) {
            pc++;
            continue;
        }

        // 5. Parse executing statements
        if (line.startsWith("var ")) {
            int eqIdx = line.indexOf('=');
            if (eqIdx < 0) {
                errorOut = "Syntax error on line " + String(pc + 1) + ": Missing '=' in variable declaration.";
                return false;
            }
            String varName = line.substring(4, eqIdx);
            varName.trim();
            String expr = line.substring(eqIdx + 1);
            expr.trim();

            bool evalOk = false;
            String val = evaluateExpression(expr, evalOk, errorOut);
            if (!evalOk) {
                errorOut = "Line " + String(pc + 1) + ": " + errorOut;
                return false;
            }

            // Detect if numeric
            bool isNumeric = true;
            if (val.length() == 0) isNumeric = false;
            for (size_t i = 0; i < val.length(); ++i) {
                char c = val[i];
                if (!isDigit(c) && c != '.' && c != '-') {
                    isNumeric = false;
                    break;
                }
            }

            if (isNumeric) {
                setVar(varName, val.toFloat());
            } else {
                setVar(varName, val);
            }
        }
        else if (line.startsWith("delay(")) {
            int endParen = line.indexOf(')');
            if (endParen < 0) {
                errorOut = "Syntax error on line " + String(pc + 1) + ": Missing ')' in delay call.";
                return false;
            }
            String delayValStr = line.substring(6, endParen);
            delayValStr.trim();
            
            bool evalOk = false;
            String res = evaluateExpression(delayValStr, evalOk, errorOut);
            if (!evalOk) return false;
            
            int delayMs = res.toInt();
            if (delayMs > 0) {
                delay(delayMs);
            }
        }
        else if (line.startsWith("call ")) {
            bool callOk = false;
            executeCall(line, callOk, errorOut);
            if (!callOk) {
                errorOut = "Line " + String(pc + 1) + ": " + errorOut;
                return false;
            }
        }
        else {
            errorOut = "Syntax error on line " + String(pc + 1) + ": Unknown statement: '" + line + "'";
            return false;
        }

        pc++;
    }

    if (_blockDepth > 0) {
        errorOut = "Syntax error: Missing closing brace '}' at end of script.";
        return false;
    }

    return true;
}

String ZwinVM::evaluateExpression(const String& expr, bool& success, String& err) {
    success = true;
    
    // Check if it is a function call
    if (expr.startsWith("call ")) {
        return executeCall(expr, success, err);
    }

    // Check if it is a quoted string literal
    if (expr.startsWith("\"") && expr.endsWith("\"")) {
        return expr.substring(1, expr.length() - 1);
    }

    // Check if it is an existing variable
    int varIdx = findVariable(expr);
    if (varIdx >= 0) {
        if (_variables[varIdx].isNum) {
            return String(_variables[varIdx].numVal);
        } else {
            return _variables[varIdx].strVal;
        }
    }

    // Otherwise, check if it is a raw numeric literal
    bool isNumeric = true;
    for (size_t i = 0; i < expr.length(); ++i) {
        char c = expr[i];
        if (!isDigit(c) && c != '.' && c != '-') {
            isNumeric = false;
            break;
        }
    }

    if (isNumeric && expr.length() > 0) {
        return expr;
    }

    // Variable or expression is invalid
    success = false;
    err = "Invalid expression: '" + expr + "'";
    return "";
}

bool ZwinVM::evaluateCondition(const String& cond, bool& success, String& err) {
    success = true;
    
    // Support operators: ==, !=, <, >, <=, >=
    String op = "";
    int opIdx = -1;
    int opLen = 2;

    if ((opIdx = cond.indexOf("==")) >= 0) op = "==";
    else if ((opIdx = cond.indexOf("!=")) >= 0) op = "!=";
    else if ((opIdx = cond.indexOf("<=")) >= 0) op = "<=";
    else if ((opIdx = cond.indexOf(">=")) >= 0) op = ">=";
    else if ((opIdx = cond.indexOf('<')) >= 0) { op = "<"; opLen = 1; }
    else if ((opIdx = cond.indexOf('>')) >= 0) { op = ">"; opLen = 1; }

    if (opIdx < 0) {
        // Evaluate single expression (e.g. if x)
        bool exprOk = false;
        String val = evaluateExpression(cond, exprOk, err);
        if (!exprOk) {
            success = false;
            return false;
        }
        return val.toFloat() != 0.0f || val == "true";
    }

    String lhs = cond.substring(0, opIdx);
    String rhs = cond.substring(opIdx + opLen);
    lhs.trim();
    rhs.trim();

    bool lhsOk = false, rhsOk = false;
    String lhsVal = evaluateExpression(lhs, lhsOk, err);
    if (!lhsOk) { success = false; return false; }
    
    String rhsVal = evaluateExpression(rhs, rhsOk, err);
    if (!rhsOk) { success = false; return false; }

    // Evaluate based on strings or floats
    bool isLhsFloat = true, isRhsFloat = true;
    for (size_t i = 0; i < lhsVal.length(); ++i) {
        if (!isDigit(lhsVal[i]) && lhsVal[i] != '.' && lhsVal[i] != '-') { isLhsFloat = false; break; }
    }
    for (size_t i = 0; i < rhsVal.length(); ++i) {
        if (!isDigit(rhsVal[i]) && rhsVal[i] != '.' && rhsVal[i] != '-') { isRhsFloat = false; break; }
    }

    if (isLhsFloat && isRhsFloat && lhsVal.length() > 0 && rhsVal.length() > 0) {
        float f1 = lhsVal.toFloat();
        float f2 = rhsVal.toFloat();

        if (op == "==") return f1 == f2;
        if (op == "!=") return f1 != f2;
        if (op == "<")  return f1 < f2;
        if (op == ">")  return f1 > f2;
        if (op == "<=") return f1 <= f2;
        if (op == ">=") return f1 >= f2;
    } else {
        if (op == "==") return lhsVal == rhsVal;
        if (op == "!=") return lhsVal != rhsVal;
        
        success = false;
        err = "Unsupported string comparison operator: " + op;
        return false;
    }

    return false;
}

String ZwinVM::executeCall(const String& callStr, bool& success, String& err) {
    success = true;
    
    // Call pattern: call module.action(param1=val, param2=val)
    int callOffset = callStr.startsWith("call ") ? 5 : 0;
    int parenOpen = callStr.indexOf('(');
    if (parenOpen < 0) {
        success = false;
        err = "Missing opening parenthesis '(' in call statement.";
        return "";
    }

    String funcName = callStr.substring(callOffset, parenOpen);
    funcName.trim();

    int parenClose = callStr.lastIndexOf(')');
    if (parenClose < 0 || parenClose < parenOpen) {
        success = false;
        err = "Missing closing parenthesis ')' in call statement.";
        return "";
    }

    String params = callStr.substring(parenOpen + 1, parenClose);
    params.trim();

    // Check if the dynamic variables inside parameters need evaluation!
    // E.g. call motors.drive(speed=x) should replace 'x' with its actual evaluated string
    String evaluatedParams = "";
    int paramIdx = 0;
    while (paramIdx < (int)params.length()) {
        int eqIdx = params.indexOf('=', paramIdx);
        if (eqIdx < 0) {
            evaluatedParams += params.substring(paramIdx);
            break;
        }
        
        evaluatedParams += params.substring(paramIdx, eqIdx + 1); // include key and '='
        
        int commaIdx = params.indexOf(',', eqIdx);
        String rawVal = "";
        if (commaIdx < 0) {
            rawVal = params.substring(eqIdx + 1);
            paramIdx = params.length();
        } else {
            rawVal = params.substring(eqIdx + 1, commaIdx);
            paramIdx = commaIdx; // will parse ',' next
        }
        rawVal.trim();

        bool valOk = false;
        String val = evaluateExpression(rawVal, valOk, err);
        if (!valOk) {
            success = false;
            return "";
        }
        
        // Wrap strings in double quotes for easier parsing in bindings
        bool needsQuotes = true;
        for (size_t i = 0; i < val.length(); ++i) {
            if (!isDigit(val[i]) && val[i] != '.' && val[i] != '-') {
                needsQuotes = false; // already string format inside VM expression evaluator
                break;
            }
        }
        if (needsQuotes && val.length() > 0) {
            evaluatedParams += val;
        } else {
            evaluatedParams += "\"" + val + "\"";
        }
        
        if (commaIdx >= 0) {
            evaluatedParams += ", ";
            paramIdx++; // skip ','
        }
    }

    ZwinFunc f = findFunction(funcName);
    if (!f) {
        success = false;
        err = "Call failed: function '" + funcName + "' is not registered.";
        return "";
    }

    return f(evaluatedParams);
}

// SDK helper: extracts parameter values by key, e.g. from "speed=100, direction=1"
String ZwinVM::getParamValue(const String& params, const String& key) {
    int keyIdx = params.indexOf(key + "=");
    if (keyIdx < 0) return "";

    int valStart = keyIdx + key.length() + 1;
    int commaIdx = params.indexOf(',', valStart);
    String rawVal = "";
    if (commaIdx < 0) {
        rawVal = params.substring(valStart);
    } else {
        rawVal = params.substring(valStart, commaIdx);
    }
    rawVal.trim();

    if (rawVal.startsWith("\"") && rawVal.endsWith("\"")) {
        return rawVal.substring(1, rawVal.length() - 1);
    }
    return rawVal;
}
