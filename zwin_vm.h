#pragma once
#include <Arduino.h>

// Function pointer signature for registered hardware/API actions
typedef String (*ZwinFunc)(const String& params);

// A simple key-value store for variables inside the script
struct ZwinVar {
    String name;
    String strVal;
    float numVal = 0.0f;
    bool isNum = true;

    ZwinVar() {}
    ZwinVar(const String& n, const String& s, float num, bool num_flag) 
        : name(n), strVal(s), numVal(num), isNum(num_flag) {}
};

// Represents a nesting block (if statement or loop)
struct ZwinBlock {
    enum Type { BLOCK_IF, BLOCK_ELSE, BLOCK_LOOP };
    Type type;
    bool active = true;        // If false, lines inside this block are skipped
    int loopStartLine = -1;    // Position in line array to jump back to
    int loopCount = 0;         // Remaining iterations

    ZwinBlock() {}
    ZwinBlock(Type t, bool act, int start, int count) 
        : type(t), active(act), loopStartLine(start), loopCount(count) {}
};

class ZwinVM {
public:
    ZwinVM();
    void begin();
    
    // Executes a Zwin script. Returns true on success, false on syntax/runtime error.
    bool execute(const String& script, String& errorOut);
    
    // Register custom C++ functions (SDK calls)
    void registerFunction(const String& name, ZwinFunc func);
    
    // Variable helpers
    void setVar(const String& name, float val);
    void setVar(const String& name, const String& val);
    float getVarNum(const String& name, float defaultVal = 0.0f);
    String getVarStr(const String& name, const String& defaultVal = "");

private:
    static const int MAX_VARIABLES = 32;
    static const int MAX_BLOCK_DEPTH = 8;
    static const int MAX_FUNCTIONS = 32;

    ZwinVar _variables[MAX_VARIABLES];
    int _varCount = 0;

    ZwinBlock _blockStack[MAX_BLOCK_DEPTH];
    int _blockDepth = 0;

    struct RegFunc {
        String name;
        ZwinFunc func;
    } _registeredFuncs[MAX_FUNCTIONS];
    int _funcCount = 0;

    // Helper evaluation methods
    String evaluateExpression(const String& expr, bool& success, String& err);
    bool evaluateCondition(const String& cond, bool& success, String& err);
    String executeCall(const String& callStr, bool& success, String& err);
    
    int findVariable(const String& name);
    ZwinFunc findFunction(const String& name);
    
    String getParamValue(const String& params, const String& key);
};
