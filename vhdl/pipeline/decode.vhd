----------------------------------------------------------------------------------------------------
-- Copyright (c) 2018 Marcus Geelnard
--
-- This software is provided 'as-is', without any express or implied warranty. In no event will the
-- authors be held liable for any damages arising from the use of this software.
--
-- Permission is granted to anyone to use this software for any purpose, including commercial
-- applications, and to alter it and redistribute it freely, subject to the following restrictions:
--
--  1. The origin of this software must not be misrepresented; you must not claim that you wrote
--     the original software. If you use this software in a product, an acknowledgment in the
--     product documentation would be appreciated but is not required.
--
--  2. Altered source versions must be plainly marked as such, and must not be misrepresented as
--     being the original software.
--
--  3. This notice may not be removed or altered from any source distribution.
----------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------
-- Pipeline Stage 3: Instruction Decode (ID)
--
-- Note: This entity also implements the WB stage (stage 5), since the register files live here.
----------------------------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.common.all;

entity decode is
  port(
      -- Control signals.
      i_clk : in std_logic;
      i_rst : in std_logic;
      i_stall : in std_logic;
      o_stall : out std_logic;
      i_cancel : in std_logic;

      -- From the IF stage (sync).
      i_pc : in std_logic_vector(C_WORD_SIZE-1 downto 0);
      i_instr : in std_logic_vector(C_WORD_SIZE-1 downto 0);
      i_bubble : in std_logic;  -- 1 if IF could not provide a new instruction.

      -- Operand forwarding to the branch logic.
      i_branch_fwd_value : in std_logic_vector(C_WORD_SIZE-1 downto 0);
      i_branch_fwd_use_value : std_logic;
      i_branch_fwd_value_ready : std_logic;

      -- Operand forwarding to EX input.
      i_reg_a_fwd_value : in std_logic_vector(C_WORD_SIZE-1 downto 0);
      i_reg_a_fwd_use_value : in std_logic;
      i_reg_a_fwd_value_ready : in std_logic;
      i_reg_b_fwd_value : in std_logic_vector(C_WORD_SIZE-1 downto 0);
      i_reg_b_fwd_use_value : in std_logic;
      i_reg_b_fwd_value_ready : in std_logic;
      i_reg_c_fwd_value : in std_logic_vector(C_WORD_SIZE-1 downto 0);
      i_reg_c_fwd_use_value : in std_logic;
      i_reg_c_fwd_value_ready : in std_logic;

      -- WB data from the MEM stage (sync).
      i_wb_we : in std_logic;
      i_wb_data_w : in std_logic_vector(C_WORD_SIZE-1 downto 0);
      i_wb_sel_w : in std_logic_vector(C_LOG2_NUM_REGS-1 downto 0);

      -- Branch results to the EX stage (sync).
      o_branch_reg_addr : out std_logic_vector(C_WORD_SIZE-1 downto 0);
      o_branch_offset_addr : out std_logic_vector(C_WORD_SIZE-1 downto 0);
      o_branch_is_branch : out std_logic;
      o_branch_is_reg : out std_logic;  -- 1 for register branches, 0 for all other instructions.
      o_branch_is_taken : out std_logic;

      -- To the EX stage (sync).
      o_pc : out std_logic_vector(C_WORD_SIZE-1 downto 0);
      o_src_a : out std_logic_vector(C_WORD_SIZE-1 downto 0);
      o_src_b : out std_logic_vector(C_WORD_SIZE-1 downto 0);
      o_src_c : out std_logic_vector(C_WORD_SIZE-1 downto 0);
      o_dst_reg : out std_logic_vector(C_LOG2_NUM_REGS-1 downto 0);
      o_writes_to_reg : out std_logic;
      o_alu_op : out T_ALU_OP;
      o_mem_op : out T_MEM_OP;
      o_mul_op : out T_MUL_OP;
      o_div_op : out T_DIV_OP;
      o_alu_en : out std_logic;
      o_mem_en : out std_logic;
      o_mul_en : out std_logic;
      o_div_en : out std_logic
    );
end decode;

architecture rtl of decode is
  -- Instruction decode signals.
  signal s_op_high : std_logic_vector(5 downto 0);
  signal s_op_low : std_logic_vector(8 downto 0);
  signal s_reg_a : std_logic_vector(C_LOG2_NUM_REGS-1 downto 0);
  signal s_reg_b : std_logic_vector(C_LOG2_NUM_REGS-1 downto 0);
  signal s_reg_c : std_logic_vector(C_LOG2_NUM_REGS-1 downto 0);
  signal s_imm : std_logic_vector(C_WORD_SIZE-1 downto 0);

  signal s_is_type_a : std_logic;
  signal s_is_type_b : std_logic;
  signal s_is_type_c : std_logic;

  signal s_is_reg_branch : std_logic;
  signal s_is_offset_branch : std_logic;
  signal s_is_branch : std_logic;
  signal s_is_link_branch : std_logic;

  signal s_mem_op_type : std_logic_vector(1 downto 0);
  signal s_is_mem_op : std_logic;
  signal s_is_mem_store : std_logic;

  signal s_is_mul_op : std_logic;
  signal s_is_div_op : std_logic;
  signal s_is_fpu_op : std_logic;

  signal s_is_ldhi : std_logic;
  signal s_is_ldhio : std_logic;
  signal s_is_ldi : std_logic;

  -- Branch condition signals.
  signal s_branch_cond_eq : std_logic;
  signal s_branch_cond_ne : std_logic;
  signal s_branch_cond_lt : std_logic;
  signal s_branch_cond_le : std_logic;
  signal s_branch_cond_ge : std_logic;
  signal s_branch_cond_gt : std_logic;
  signal s_branch_cond_true : std_logic;

  -- Branch target signals.
  signal s_branch_offset_addr : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_branch_reg_data : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_branch_is_taken : std_logic;

  -- Register read signals.
  signal s_reg_a_data : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_reg_b_data : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_reg_c_data : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_vl_data : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_reg_a_required : std_logic;
  signal s_reg_b_required : std_logic;
  signal s_reg_c_required : std_logic;

  -- Signals to the EX stage.
  signal s_src_a : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_src_b : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_src_c : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_dst_reg : std_logic_vector(C_LOG2_NUM_REGS-1 downto 0);
  signal s_writes_to_reg : std_logic;
  signal s_alu_op : T_ALU_OP;
  signal s_mem_op : T_MEM_OP;
  signal s_mul_op : T_MUL_OP;
  signal s_div_op : T_DIV_OP;
  signal s_alu_en : std_logic;
  signal s_mem_en : std_logic;
  signal s_mul_en : std_logic;
  signal s_div_en : std_logic;

  -- Operand forwarding signals.
  signal s_reg_a_data_or_fwd : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_reg_b_data_or_fwd : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_reg_c_data_or_fwd : std_logic_vector(C_WORD_SIZE-1 downto 0);
  signal s_missing_fwd_operand : std_logic;

  -- Signals for handling discarding of the current operation (i.e. bubble).
  signal s_bubble : std_logic;
  signal s_writes_to_reg_masked : std_logic;
  signal s_dst_reg_masked : std_logic_vector(C_LOG2_NUM_REGS-1 downto 0);
  signal s_alu_op_masked : T_ALU_OP;
  signal s_mem_op_masked : T_MEM_OP;
  signal s_alu_en_masked : std_logic;
  signal s_mem_en_masked : std_logic;
  signal s_mul_en_masked : std_logic;
  signal s_div_en_masked : std_logic;
  signal s_is_branch_masked : std_logic;
begin
  -- Extract operation codes.
  s_op_high <= i_instr(29 downto 24);
  s_op_low <= i_instr(8 downto 0);

  -- Determine encoding type.
  s_is_type_a <= '1' when s_op_high = "000000" else '0';
  s_is_type_c <= '1' when s_op_high(5 downto 4) = "11" else '0';
  s_is_type_b <= not (s_is_type_a or s_is_type_c);

  -- Extract immediate.
  s_imm(13 downto 0) <= i_instr(13 downto 0);
  s_imm(18 downto 14) <= i_instr(18 downto 14) when s_is_type_c = '1' else (others => i_instr(13));
  s_imm(31 downto 19) <= (others => s_imm(18));

  -- Extract register numbers.
  s_reg_a <= i_instr(18 downto 14);
  s_reg_b <= i_instr(13 downto 9);
  s_reg_c <= i_instr(23 downto 19);  -- Usually destination, somtimes source.

  -- Read from the register file.
  regs_scalar_1: entity work.regs_scalar
    port map (
      i_clk => i_clk,
      i_rst => i_rst,
      i_sel_a => s_reg_a,
      i_sel_b => s_reg_b,
      i_sel_c => s_reg_c,
      o_data_a => s_reg_a_data,
      o_data_b => s_reg_b_data,
      o_data_c => s_reg_c_data,
      o_vl => s_vl_data,
      i_we => i_wb_we,
      i_data_w => i_wb_data_w,
      i_sel_w => i_wb_sel_w,
      i_pc => i_pc
    );


  --------------------------------------------------------------------------------------------------
  -- Branch logic.
  --------------------------------------------------------------------------------------------------

  -- Is this a branch?
  s_is_reg_branch <= (s_is_type_a and not i_bubble) when s_op_low(8 downto 1) = "00111000" else '0';  -- J, JL

  IsOffsetBranchMux: with s_op_high select
    s_is_offset_branch <=
        (not i_bubble) when "110000" | "110001" | "110010" | "110011" | "110100" | "110101" |  -- B[cc]
                            "111000" | "111001" | "111010" | "111011" | "111100" | "111101",   -- BL[cc]
        '0' when others;

  s_is_branch <= s_is_reg_branch or s_is_offset_branch;

  -- Is this a link branch.
  s_is_link_branch <= (s_is_reg_branch and s_op_low(0)) or (s_is_offset_branch and s_op_high(3));

  -- Calculate the offset branch target.
  pc_plus_offset_0: entity work.pc_plus_offset
    port map (
      i_pc => i_pc,
      i_offset => i_instr(18 downto 0),
      o_result => s_branch_offset_addr
    );

  -- Get the register content for branch logic (condition or target address).
  s_branch_reg_data <= i_branch_fwd_value when i_branch_fwd_use_value = '1' else s_reg_c_data;

  -- Determine if a conditional (offset) branch is taken?
  branch_comparator_0: entity work.comparator
    generic map (WIDTH => C_WORD_SIZE)
    port map (
      i_src => s_branch_reg_data,
      o_eq => s_branch_cond_eq,
      o_ne => s_branch_cond_ne,
      o_lt => s_branch_cond_lt,
      o_le => s_branch_cond_le,
      o_gt => s_branch_cond_gt,
      o_ge => s_branch_cond_ge
    );

  BranchCondMux: with s_op_high(2 downto 0) select
    s_branch_cond_true <=
        s_branch_cond_eq when "000",  -- BEQ
        s_branch_cond_ne when "001",  -- BNE
        s_branch_cond_ge when "010",  -- BGE
        s_branch_cond_gt when "011",  -- BGT
        s_branch_cond_le when "100",  -- BLE
        s_branch_cond_lt when "101",  -- BLT
        '0' when others;

  s_branch_is_taken <= s_is_reg_branch or (s_is_offset_branch and s_branch_cond_true);


  --------------------------------------------------------------------------------------------------
  -- Prepare data for the EX stage.
  --------------------------------------------------------------------------------------------------

  -- Determine MEM operation.
  s_mem_op_type(0) <= s_is_type_a when (s_op_low(8 downto 4) = "00000") else '0';
  s_mem_op_type(1) <= s_is_type_b when (s_op_high(5 downto 4) = "00") else '0';
  MemOpMux: with s_mem_op_type select
    s_mem_op <=
        s_op_low(3 downto 0) when "01",    -- Addr = reg + reg
        s_op_high(3 downto 0) when "10",   -- Addr = reg + imm
        (others => '0') when others;
  s_is_mem_op <= s_mem_op_type(0) or s_mem_op_type(1);
  s_is_mem_store <= s_is_mem_op and s_mem_op(3);

  -- Is this an immediate load?
  s_is_ldhi  <= '1' when s_op_high = "110110" else '0';
  s_is_ldhio <= '1' when s_op_high = "110111" else '0';
  s_is_ldi   <= '1' when s_op_high = "111110" else '0';

  -- Is this a MUL, DIV or FPU op?
  s_is_mul_op <= '1' when (s_is_type_a = '1' and s_op_low(8 downto 3) = "010000") else '0';
  s_is_div_op <= '1' when (s_is_type_a = '1' and s_op_low(8 downto 3) = "010001") else '0';
  s_is_fpu_op <= '1' when (s_is_type_a = '1' and s_op_low(8 downto 4) = "01001") else '0';

  -- What source registers are required for this operation?
  s_reg_a_required <= not s_is_type_c;
  s_reg_b_required <= s_is_type_a;
  s_reg_c_required <= s_is_mem_store;

  -- Select data from the register file or operand forwarding.
  s_reg_a_data_or_fwd <= i_reg_a_fwd_value when i_reg_a_fwd_use_value = '1' else s_reg_a_data;
  s_reg_b_data_or_fwd <= i_reg_b_fwd_value when i_reg_b_fwd_use_value = '1' else s_reg_b_data;
  s_reg_c_data_or_fwd <= i_reg_c_fwd_value when i_reg_c_fwd_use_value = '1' else s_reg_c_data;

  -- Select source data for the EX stage.
  -- Note: For linking branches we use the ALU to calculate PC + 4.
  s_src_a <= i_pc when s_is_link_branch = '1' else
             s_reg_a_data_or_fwd when s_is_type_c = '0' else
             s_imm;
  s_src_b <= X"00000004" when s_is_link_branch = '1' else
             s_reg_b_data_or_fwd when s_is_type_a = '1' else
             s_imm;
  s_src_c <= s_reg_c_data_or_fwd;

  -- Select destination register.
  -- Note: For linking branches we set the target register to LR.
  s_dst_reg <= to_vector(C_LR_REG, C_LOG2_NUM_REGS) when s_is_link_branch = '1' else
               s_reg_c when (s_is_mem_store or s_is_branch) = '0' else
               (others => '0');

  -- What pipeline units should be enabled?
  s_alu_en <= not (s_is_mul_op or s_is_div_op or s_is_fpu_op);
  s_mul_en <= s_is_mul_op;
  s_div_en <= s_is_div_op;
  s_mem_en <= s_is_mem_op;

  -- Select ALU operation.
  s_alu_op <=
      -- If this is not an ALU operation, disable the ALU.
      C_ALU_CPUID when s_alu_en = '0' else

      -- Use the ALU to calculate the memory/return address.
      C_ALU_ADD when (s_is_mem_op or s_is_link_branch) = '1' else

      -- Use NOP for non-linking branches (they do not produce any result).
      C_ALU_CPUID when s_is_branch = '1' else

      -- LDHI has a special ALU op.
      C_ALU_LDHI when s_is_ldhi = '1' else

      -- LDHIO has a special ALU op.
      C_ALU_LDHIO when s_is_ldhio = '1' else

      -- LDI can use the OR operator (i.e. just move the immediate value to the target reg).
      C_ALU_OR when s_is_ldi = '1' else

      -- Map the low order bits of the low order opcode directly to the ALU.
      s_op_low(C_ALU_OP_SIZE-1 downto 0) when s_is_type_a = '1' else

      -- Map the high order opcode directly to the ALU.
      s_op_high;

  -- Select multiply operation.
  -- Map the low order bits of the low order opcode directly to the multiply unit.
  s_mul_op <= s_op_low(C_MUL_OP_SIZE-1 downto 0);

  -- Select division operation.
  -- Map the low order bits of the low order opcode directly to the division unit.
  s_mul_op <= s_op_low(C_DIV_OP_SIZE-1 downto 0);

  -- Are we missing any fwd operation that has not yet been produced by the pipeline?
  s_missing_fwd_operand <=
      (s_is_branch and (i_branch_fwd_use_value and not i_branch_fwd_value_ready)) or
      (s_reg_a_required and (i_reg_a_fwd_use_value and not i_reg_a_fwd_value_ready)) or
      (s_reg_b_required and (i_reg_b_fwd_use_value and not i_reg_b_fwd_value_ready)) or
      (s_reg_c_required and (i_reg_c_fwd_use_value and not i_reg_c_fwd_value_ready));

  -- Should we discard the operation (i.e. send a bubble down the pipeline)?
  s_bubble <= i_bubble or i_cancel or s_missing_fwd_operand;
  s_dst_reg_masked <= s_dst_reg when s_bubble = '0' else (others => '0');
  s_writes_to_reg_masked <= s_writes_to_reg and not s_bubble;
  s_alu_op_masked <= s_alu_op when s_bubble = '0' else (others => '0');
  s_mem_op_masked <= s_mem_op when s_bubble = '0' else (others => '0');
  s_alu_en_masked <= s_alu_en and not s_bubble;
  s_mem_en_masked <= s_mem_en and not s_bubble;
  s_mul_en_masked <= s_mul_en and not s_bubble;
  s_div_en_masked <= s_div_en and not s_bubble;
  s_is_branch_masked <= s_is_branch and not s_bubble;

  -- Will this instruction write to a register?
  s_writes_to_reg <= '1' when ((s_dst_reg_masked /= to_vector(C_Z_REG, C_LOG2_NUM_REGS)) and
                               (s_dst_reg_masked /= to_vector(C_PC_REG, C_LOG2_NUM_REGS))) else '0';

  -- Outputs to the EX stage.
  process(i_clk, i_rst)
  begin
    if i_rst = '1' then
      o_src_a <= (others => '0');
      o_src_b <= (others => '0');
      o_src_c <= (others => '0');
      o_dst_reg <= (others => '0');
      o_writes_to_reg <= '0';
      o_alu_op <= (others => '0');
      o_mem_op <= (others => '0');
      o_mul_op <= (others => '0');
      o_div_op <= (others => '0');
      o_alu_en <= '0';
      o_mem_en <= '0';
      o_mul_en <= '0';
      o_div_en <= '0';

      o_branch_reg_addr <= (others => '0');
      o_branch_offset_addr <= (others => '0');
      o_branch_is_branch <= '0';
      o_branch_is_reg <= '0';
      o_branch_is_taken <= '0';
    elsif rising_edge(i_clk) then
      if i_stall = '0' then
        o_pc <= i_pc;
        o_src_a <= s_src_a;
        o_src_b <= s_src_b;
        o_src_c <= s_src_c;
        o_dst_reg <= s_dst_reg_masked;
        o_writes_to_reg <= s_writes_to_reg_masked;
        o_alu_op <= s_alu_op_masked;
        o_mem_op <= s_mem_op_masked;
        o_mul_op <= s_mul_op;
        o_div_op <= s_div_op;
        o_alu_en <= s_alu_en_masked;
        o_mem_en <= s_mem_en_masked;
        o_mul_en <= s_mul_en_masked;
        o_div_en <= s_div_en_masked;

        o_branch_reg_addr <= s_branch_reg_data;
        o_branch_offset_addr <= s_branch_offset_addr;
        o_branch_is_branch <= s_is_branch_masked;
        o_branch_is_reg <= s_is_reg_branch;
        o_branch_is_taken <= s_branch_is_taken;
      end if;
    end if;
  end process;

  -- Do we need to stall the pipeline (async)?
  o_stall <= s_missing_fwd_operand;
end rtl;
