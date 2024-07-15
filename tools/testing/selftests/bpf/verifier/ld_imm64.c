{
	"test1 ld_imm64",
	.insns = {
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
	BPF_LD_IMM64(BPF_REG_0, 0),
	BPF_LD_IMM64(BPF_REG_0, 0),
	BPF_LD_IMM64(BPF_REG_0, 1),
	BPF_LD_IMM64(BPF_REG_0, 1),
	BPF_MOV64_IMM(BPF_REG_0, 2),
	BPF_EXIT_INSN(),
	},
	.errstr = "jump into the middle of ldimm64 insn 1",
	.errstr_unpriv = "jump into the middle of ldimm64 insn 1",
	.result = REJECT,
},
{
	"test2 ld_imm64",
	.insns = {
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
	BPF_LD_IMM64(BPF_REG_0, 0),
	BPF_LD_IMM64(BPF_REG_0, 0),
	BPF_LD_IMM64(BPF_REG_0, 1),
	BPF_LD_IMM64(BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.errstr = "jump into the middle of ldimm64 insn 1",
	.errstr_unpriv = "jump into the middle of ldimm64 insn 1",
	.result = REJECT,
},
{
	"test3 ld_imm64",
	.insns = {
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 1),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 0, 0),
	BPF_LD_IMM64(BPF_REG_0, 0),
	BPF_LD_IMM64(BPF_REG_0, 0),
	BPF_LD_IMM64(BPF_REG_0, 1),
	BPF_LD_IMM64(BPF_REG_0, 1),
	BPF_EXIT_INSN(),
	},
	.errstr = "invalid bpf_ld_imm64 insn",
	.result = REJECT,
},
{
	"test4 ld_imm64",
	.insns = {
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "invalid bpf_ld_imm64 insn",
	.result = REJECT,
},
{
	"test6 ld_imm64",
	.insns = {
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 0, 0),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
},
{
	"test7 ld_imm64",
	.insns = {
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 0, 1),
	BPF_RAW_INSN(0, 0, 0, 0, 1),
	BPF_EXIT_INSN(),
	},
	.result = ACCEPT,
	.retval = 1,
},
{
	"test8 ld_imm64",
	.insns = {
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 1, 1),
	BPF_RAW_INSN(0, 0, 0, 0, 1),
	BPF_EXIT_INSN(),
	},
	.errstr = "uses reserved fields",
	.result = REJECT,
},
{
	"test9 ld_imm64",
	.insns = {
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 0, 1),
	BPF_RAW_INSN(0, 0, 0, 1, 1),
	BPF_EXIT_INSN(),
	},
	.errstr = "invalid bpf_ld_imm64 insn",
	.result = REJECT,
},
{
	"test10 ld_imm64",
	.insns = {
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 0, 1),
	BPF_RAW_INSN(0, BPF_REG_1, 0, 0, 1),
	BPF_EXIT_INSN(),
	},
	.errstr = "invalid bpf_ld_imm64 insn",
	.result = REJECT,
},
{
	"test11 ld_imm64",
	.insns = {
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 0, 1),
	BPF_RAW_INSN(0, 0, BPF_REG_1, 0, 1),
	BPF_EXIT_INSN(),
	},
	.errstr = "invalid bpf_ld_imm64 insn",
	.result = REJECT,
},
{
	"test12 ld_imm64",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, BPF_REG_1, 0, 1),
	BPF_RAW_INSN(0, 0, 0, 0, 0),
	BPF_EXIT_INSN(),
	},
	.errstr = "not pointing to valid bpf_map",
	.result = REJECT,
},
{
	"test13 ld_imm64",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_1, 0),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, BPF_REG_1, 0, 1),
	BPF_RAW_INSN(0, 0, BPF_REG_1, 0, 1),
	BPF_EXIT_INSN(),
	},
	.errstr = "invalid bpf_ld_imm64 insn",
	.result = REJECT,
},
{
	"test14 ld_imm64: reject 2nd imm != 0",
	.insns = {
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, BPF_REG_1,
		     BPF_PSEUDO_MAP_FD, 0, 0),
	BPF_RAW_INSN(0, 0, 0, 0, 0xfefefe),
	BPF_EXIT_INSN(),
	},
	.fixup_map_hash_48b = { 1 },
	.errstr = "unrecognized bpf_ld_imm64 insn",
	.result = REJECT,
},
