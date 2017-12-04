#include "listing.h"

namespace REDasm {

Listing::Listing(): cache_map<address_t, InstructionPtr>("instructions"), _processor(NULL), _referencetable(NULL), _symboltable(NULL)
{

}

Listing::~Listing()
{

}

ReferenceTable *Listing::referenceTable() const
{
    return this->_referencetable;
}

SymbolTable *Listing::symbolTable() const
{
    return this->_symboltable;
}

FormatPlugin *Listing::format() const
{
    return this->_format;
}

const ProcessorPlugin* Listing::processor() const
{
    return this->_processor;
}

void Listing::setFormat(FormatPlugin *format)
{
    this->_format = format;
}

void Listing::setProcessor(ProcessorPlugin *processor)
{
    this->_processor = processor;
}

void Listing::setSymbolTable(SymbolTable *symboltable)
{
    this->_symboltable = symboltable;
}

void Listing::setReferenceTable(ReferenceTable *referencetable)
{
    this->_referencetable = referencetable;
}

void Listing::walk(address_t address)
{
    if(!this->_processor)
        return;

    FunctionPath path;
    this->walk(this->find(address), path);

    if(!path.empty())
        this->_paths[address] = path;
}

void Listing::walk(Listing::iterator it, FunctionPath& path)
{
    if((it == this->end()) || (path.find(it.key) != path.end())) // Don't reanalyze same paths
        return;

    path.insert(it.key);

    address_t target = 0;
    InstructionPtr instruction = *it;

    if(instruction->is(InstructionTypes::Stop))
        return;

    if(instruction->is(InstructionTypes::Jump) && this->_processor->target(instruction, &target))
    {
        SymbolPtr symbol = this->_symboltable->symbol(target);
        auto targetit = this->find(target);

        if((!symbol || !symbol->isFunction()) && (targetit != this->end()))
            this->walk(targetit, path);

        if(!instruction->is(InstructionTypes::Conditional)) // Unconditional jumps doesn't continue execution
            return;
    }

    it++;

    if((it != this->end()) && !this->isFunctionStart((*it)->address))
        this->walk(it, path);
}

std::string Listing::getSignature(const SymbolPtr& symbol)
{
    std::string sig;
    auto it = this->_paths.find(symbol->address);

    if(it == this->_paths.end())
        return std::string();

    const std::set<address_t> path = it->second;

    std::for_each(path.begin(), path.end(), [this, &sig](address_t address) {
        InstructionPtr instruction = (*this)[address];
        sig += instruction->signature;
    });

    return sig;
}

bool Listing::iterateFunction(address_t address, Listing::InstructionCallback cbinstruction)
{
    return this->iterateFunction(address, cbinstruction,
                                 [](const SymbolPtr&) { },
                                 [](const InstructionPtr&) { },
                                 [](const SymbolPtr&) { });
}

bool Listing::iterateFunction(address_t address, Listing::InstructionCallback cbinstruction, Listing::SymbolCallback cbstart, Listing::InstructionCallback cbend, Listing::SymbolCallback cblabel)
{
    if(!this->_processor)
        return false;

    auto it = this->findFunction(address);

    if(it == this->_paths.end())
        return false;

    SymbolPtr symbol = NULL, functionsymbol = this->_symboltable->symbol(address);

    if(functionsymbol)
        cbstart(functionsymbol);

    const FunctionPath& path = it->second;
    InstructionPtr instruction;

    std::for_each(path.begin(), path.end(), [this, &symbol, &instruction, &cbinstruction, &cblabel](address_t address) {
        symbol = this->_symboltable->symbol(address);

        if(symbol && !symbol->isFunction() && symbol->is(SymbolTypes::Code))
            cblabel(symbol);

        instruction = (*this)[address];
        cbinstruction(instruction);
    });

    if(instruction)
        cbend(instruction);

    return true;
}

void Listing::iterateAll(InstructionCallback cbinstruction, SymbolCallback cbstart, InstructionCallback cbend, SymbolCallback cblabel)
{
    this->_symboltable->iterate(SymbolTypes::FunctionMask, [this, cbinstruction, cbstart, cbend, cblabel](const SymbolPtr& symbol) -> bool {
        this->iterateFunction(symbol->address, cbinstruction, cbstart, cbend, cblabel);
        return true;
    });
}

void Listing::update(const InstructionPtr &instruction)
{
    this->commit(instruction->address, instruction);
}

void Listing::calculatePaths()
{
    this->_symboltable->iterate(SymbolTypes::FunctionMask, [this](SymbolPtr symbol) -> bool {
        this->walk(symbol->address);
        return true;
    });
}

void Listing::serialize(const InstructionPtr &value, std::fstream &fs)
{
    Serializer::serializeScalar(fs, value->address);
    Serializer::serializeScalar(fs, value->type);
    Serializer::serializeScalar(fs, value->size);
    Serializer::serializeScalar(fs, value->id);

    Serializer::serializeString(fs, value->mnemonic);
    Serializer::serializeString(fs, value->signature);

    Serializer::serializeArray<std::vector, Operand>(fs, value->operands, [this, &fs](const Operand& op) {
        Serializer::serializeScalar(fs, op.loc_index);
        Serializer::serializeScalar(fs, op.type);
        Serializer::serializeScalar(fs, op.index);

        Serializer::serializeScalar(fs, op.reg.type);
        Serializer::serializeScalar(fs, op.reg.r);

        Serializer::serializeScalar(fs, op.mem.base);
        Serializer::serializeScalar(fs, op.mem.index);
        Serializer::serializeScalar(fs, op.mem.scale);
        Serializer::serializeScalar(fs, op.mem.displacement);

        Serializer::serializeScalar(fs, op.u_value);
    });

    Serializer::serializeArray<std::list, std::string>(fs, value->comments, [this, &fs](const std::string& s) {
        Serializer::serializeString(fs, s);
    });
}

void Listing::deserialize(InstructionPtr &value, std::fstream &fs)
{
    value = std::make_shared<Instruction>();

    Serializer::deserializeScalar(fs, &value->address);
    Serializer::deserializeScalar(fs, &value->type);
    Serializer::deserializeScalar(fs, &value->size);
    Serializer::deserializeScalar(fs, &value->id);

    Serializer::deserializeString(fs, value->mnemonic);
    Serializer::deserializeString(fs, value->signature);

    Serializer::deserializeArray<std::vector, Operand>(fs, value->operands, [this, &fs](Operand& op) {
        Serializer::deserializeScalar(fs, &op.loc_index);
        Serializer::deserializeScalar(fs, &op.type);
        Serializer::deserializeScalar(fs, &op.index);

        Serializer::deserializeScalar(fs, &op.reg.type);
        Serializer::deserializeScalar(fs, &op.reg.r);

        Serializer::deserializeScalar(fs, &op.mem.base);
        Serializer::deserializeScalar(fs, &op.mem.index);
        Serializer::deserializeScalar(fs, &op.mem.scale);
        Serializer::deserializeScalar(fs, &op.mem.displacement);

        Serializer::deserializeScalar(fs, &op.u_value);
    });

    Serializer::deserializeArray<std::list, std::string>(fs, value->comments, [this, &fs](std::string& s) {
        Serializer::deserializeString(fs, s);
    });
}

bool Listing::isFunctionStart(address_t address)
{
    SymbolPtr symbol = this->_symboltable->symbol(address);

    if(!symbol)
        return false;

    return symbol->isFunction();
}

Listing::FunctionPaths::iterator Listing::findFunction(address_t address)
{
    auto it = this->_paths.find(address);

    if(it != this->_paths.end())
        return it;

    for(it = this->_paths.begin(); it != this->_paths.end(); it++)
    {
        const FunctionPath& path = it->second;
        address_t startaddress = *path.begin(), endaddress = *path.rbegin();

        if((endaddress < address) || (startaddress > address))
            continue;

        auto pathit = path.find(address);

        if(pathit != path.end())
            return it;
    }

    return this->_paths.end();
}

}
