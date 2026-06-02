#include "Valu.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <cstdint>
#include <iostream>
#include <string>

static vluint64_t sim_time = 0;

enum AluOp : uint8_t {
    ALU_ADD    = 0,
    ALU_SUB    = 1,
    ALU_AND    = 2,
    ALU_OR     = 3,
    ALU_XOR    = 4,
    ALU_SLL    = 5,
    ALU_SRL    = 6,
    ALU_SRA    = 7,
    ALU_SLT    = 8,
    ALU_SLTU   = 9,
    ALU_COPY_B = 10
};

static void eval_and_dump(Valu* dut, VerilatedVcdC* trace) {
    dut->eval();

    if (trace) {
        trace->dump(sim_time);
    }

    sim_time++;
}

static bool check(
    Valu* dut,
    VerilatedVcdC* trace,
    const std::string& name,
    uint64_t a,
    uint64_t b,
    uint8_t op,
    uint64_t expected
) {
    dut->a_i = a;
    dut->b_i = b;
    dut->op_i = op;

    eval_and_dump(dut, trace);

    if (dut->result_o != expected) {
        std::cerr << "FAIL " << name
                  << ": a=0x" << std::hex << a
                  << " b=0x" << b
                  << " expected=0x" << expected
                  << " got=0x" << dut->result_o
                  << std::dec << std::endl;
        return false;
    }

    bool expected_zero = (expected == 0);
    if (dut->zero_o != expected_zero) {
        std::cerr << "FAIL " << name
                  << ": zero_o expected " << expected_zero
                  << " got " << static_cast<int>(dut->zero_o)
                  << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    const char* wave_path = "waves/alu.vcd";
    if (argc >= 2) {
        wave_path = argv[1];
    }

    Valu* dut = new Valu;

    Verilated::traceEverOn(true);
    VerilatedVcdC* trace = new VerilatedVcdC;
    dut->trace(trace, 99);
    trace->open(wave_path);

    bool pass = true;

    // ADD
    pass &= check(dut, trace, "ADD basic",        10, 20, ALU_ADD, 30);
    pass &= check(dut, trace, "ADD zero+zero",     0,  0, ALU_ADD,  0);
    pass &= check(dut, trace, "ADD identity",      5,  0, ALU_ADD,  5);
    pass &= check(dut, trace, "ADD wrap",  UINT64_MAX,  1, ALU_ADD,  0);

    // SUB
    pass &= check(dut, trace, "SUB basic",         20, 10, ALU_SUB, 10);
    pass &= check(dut, trace, "SUB zero",          20, 20, ALU_SUB,  0);
    pass &= check(dut, trace, "SUB underflow",      0,  1, ALU_SUB, UINT64_MAX);
    pass &= check(dut, trace, "SUB identity",       7,  0, ALU_SUB,  7);

    // AND
    pass &= check(dut, trace, "AND basic",      0b1100, 0b1010, ALU_AND, 0b1000);
    pass &= check(dut, trace, "AND all ones",   UINT64_MAX, UINT64_MAX, ALU_AND, UINT64_MAX);
    pass &= check(dut, trace, "AND zero mask",  0xDEADBEEFULL, 0, ALU_AND, 0);

    // OR
    pass &= check(dut, trace, "OR basic",       0b1100, 0b1010, ALU_OR,  0b1110);
    pass &= check(dut, trace, "OR identity",    0xABCDULL, 0, ALU_OR, 0xABCDULL);
    pass &= check(dut, trace, "OR all ones",    0, UINT64_MAX, ALU_OR, UINT64_MAX);

    // XOR
    pass &= check(dut, trace, "XOR basic",      0b1100, 0b1010, ALU_XOR, 0b0110);
    pass &= check(dut, trace, "XOR self",       0xDEADBEEFULL, 0xDEADBEEFULL, ALU_XOR, 0);
    pass &= check(dut, trace, "XOR all ones",   0, UINT64_MAX, ALU_XOR, UINT64_MAX);

    // SLL
    pass &= check(dut, trace, "SLL basic",      1, 4, ALU_SLL, 16);
    pass &= check(dut, trace, "SLL by zero",    0xABCDULL, 0, ALU_SLL, 0xABCDULL);
    pass &= check(dut, trace, "SLL by 63",      1, 63, ALU_SLL, 0x8000000000000000ULL);
    pass &= check(dut, trace, "SLL shift out",  0xFF, 63, ALU_SLL, 0x8000000000000000ULL);
    // shamt uses only b[5:0]; bit 6 is ignored
    pass &= check(dut, trace, "SLL shamt mask", 1, 64, ALU_SLL, 1);

    // SRL
    pass &= check(dut, trace, "SRL basic",      16, 2, ALU_SRL, 4);
    pass &= check(dut, trace, "SRL by zero",    0xABCDULL, 0, ALU_SRL, 0xABCDULL);
    pass &= check(dut, trace, "SRL by 63",      0x8000000000000000ULL, 63, ALU_SRL, 1);
    pass &= check(dut, trace, "SRL no sign ext",0x8000000000000000ULL, 4, ALU_SRL, 0x0800000000000000ULL);

    // SRA
    pass &= check(dut, trace, "SRA negative",   0x8000000000000000ULL, 4, ALU_SRA, 0xF800000000000000ULL);
    pass &= check(dut, trace, "SRA positive",   0x0800000000000000ULL, 4, ALU_SRA, 0x0080000000000000ULL);
    pass &= check(dut, trace, "SRA by zero",    static_cast<uint64_t>(-1), 0, ALU_SRA, UINT64_MAX);
    pass &= check(dut, trace, "SRA by 63 neg",  0x8000000000000000ULL, 63, ALU_SRA, UINT64_MAX);
    pass &= check(dut, trace, "SRA by 63 pos",  0x7FFFFFFFFFFFFFFFULL, 63, ALU_SRA, 0);

    // SLT (signed)
    pass &= check(dut, trace, "SLT signed true",  static_cast<uint64_t>(-1), 1, ALU_SLT, 1);
    pass &= check(dut, trace, "SLT signed false",  1, static_cast<uint64_t>(-1), ALU_SLT, 0);
    pass &= check(dut, trace, "SLT equal",         5, 5, ALU_SLT, 0);
    pass &= check(dut, trace, "SLT INT64_MIN < 0", 0x8000000000000000ULL, 0, ALU_SLT, 1);
    pass &= check(dut, trace, "SLT 0 < INT64_MAX", 0, 0x7FFFFFFFFFFFFFFFULL, ALU_SLT, 1);

    // SLTU (unsigned)
    pass &= check(dut, trace, "SLTU unsigned true",  1, 2, ALU_SLTU, 1);
    pass &= check(dut, trace, "SLTU unsigned false", UINT64_MAX, 1, ALU_SLTU, 0);
    pass &= check(dut, trace, "SLTU equal",          42, 42, ALU_SLTU, 0);
    pass &= check(dut, trace, "SLTU 0 < UINT64_MAX", 0, UINT64_MAX, ALU_SLTU, 1);

    // COPY_B
    pass &= check(dut, trace, "COPY_B basic",  123, 456, ALU_COPY_B, 456);
    pass &= check(dut, trace, "COPY_B zero",   999,   0, ALU_COPY_B,   0);
    pass &= check(dut, trace, "COPY_B max",      0, UINT64_MAX, ALU_COPY_B, UINT64_MAX);

    dut->final();

    trace->close();
    delete trace;
    delete dut;

    if (!pass) {
        std::cerr << "ALU TEST FAILED" << std::endl;
        return 1;
    }

    std::cout << "ALU TEST PASSED" << std::endl;
    std::cout << "Waveform written to " << wave_path << std::endl;

    return 0;
}