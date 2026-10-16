#include "asm.h"
#include <fstream>

Assembler::Assembler(const std::string &in, const std::string &out, Target *t, ObjWriter *w)
    : input(in), output(out), target(t), writer(w)
{
}

Assembler::~Assembler()
{
    delete target;
    delete writer;
}

const std::vector<std::string> &Assembler::errors() const
{
    return unit.errors;
}

bool Assembler::run()
{
    std::ifstream f(input.c_str());
    if (!f) {
        unit.errors.push_back("cannot open " + input);
        return false;
    }
    std::vector<std::string> lines;
    std::string text;
    while (std::getline(f, text)) {
        if (!text.empty() && text[text.size() - 1] == '\r')
            text.erase(text.size() - 1);
        lines.push_back(text);
    }

    /* passes until the layout settles: the first sees a forward jump as short and a forward
       label as untyped, the next knows every label from the pass before, and one more confirms
       nothing moved; a jump only ever grows, so this ends */
    std::vector<Token> tokens;
    for (int pass = 1; ; pass++) {
        unit.begin_pass(pass);
        target->begin_pass(pass);
        for (size_t i = 0; i < lines.size() && !target->finished(); i++) {
            unit.line = (int)i + 1;
            std::string err;
            if (!split_line(lines[i], tokens, err)) {
                unit.error(err);
                continue;
            }
            if (!tokens.empty())
                target->statement(unit, tokens);
        }
        if (!target->finished())
            unit.error("END expected");
        unit.resolve(*target);
        if (!target->again() && !unit.moved())
            break;
        if (pass >= 200) {
            unit.error("the layout did not settle in 200 passes");
            break;
        }
    }
    if (!unit.errors.empty())
        return false;
    std::string err;
    if (!writer->write(unit, output, err)) {
        unit.errors.push_back(err);
        return false;
    }
    return true;
}
