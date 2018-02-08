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

library ieee;
use ieee.std_logic_1164.all;
use work.consts.all;

entity shift32_tb is
end shift32_tb;

architecture behavioral of shift32_tb is
  component shift32
    port(
        i_right      : in  std_logic;
        i_arithmetic : in  std_logic;
        i_src        : in  std_logic_vector(31 downto 0);
        i_shift      : in  std_logic_vector(4 downto 0);
        o_result     : out std_logic_vector(31 downto 0)
      );
  end component;

  signal s_right      : std_logic;
  signal s_arithmetic : std_logic;
  signal s_src        : std_logic_vector(31 downto 0);
  signal s_shift      : std_logic_vector(4 downto 0);
  signal s_result     : std_logic_vector(31 downto 0);

  signal s_op : std_logic_vector(1 downto 0);
begin
  shift32_0: entity work.shift32
    port map (
      i_right => s_right,
      i_arithmetic => s_arithmetic,
      i_src => s_src,
      i_shift => s_shift,
      o_result => s_result
    );

  process
    --  The patterns to apply.
    type pattern_type is record
      -- Inputs
      right      : std_logic;
      arithmetic : std_logic;
      src        : std_logic_vector(31 downto 0);
      shift      : std_logic_vector(4 downto 0);

      -- Expected outputs
      result     : std_logic_vector(31 downto 0);
    end record;
    type pattern_array is array (natural range <>) of pattern_type;
    constant patterns : pattern_array := (
        -- LSL
        ('0', '0', "10001111000001110000001100000001", "00000", "10001111000001110000001100000001"),
        ('0', '0', "10001111000001110000001100000001", "00001", "00011110000011100000011000000010"),
        ('0', '0', "10001111000001110000001100000001", "00010", "00111100000111000000110000000100"),
        ('0', '0', "10001111000001110000001100000001", "00011", "01111000001110000001100000001000"),
        ('0', '0', "10001111000001110000001100000001", "00100", "11110000011100000011000000010000"),
        ('0', '0', "10001111000001110000001100000001", "00101", "11100000111000000110000000100000"),
        ('0', '0', "10001111000001110000001100000001", "00110", "11000001110000001100000001000000"),
        ('0', '0', "10001111000001110000001100000001", "00111", "10000011100000011000000010000000"),
        ('0', '0', "10001111000001110000001100000001", "01000", "00000111000000110000000100000000"),
        ('0', '0', "10001111000001110000001100000001", "01001", "00001110000001100000001000000000"),
        ('0', '0', "10001111000001110000001100000001", "01010", "00011100000011000000010000000000"),
        ('0', '0', "10001111000001110000001100000001", "01011", "00111000000110000000100000000000"),
        ('0', '0', "10001111000001110000001100000001", "01100", "01110000001100000001000000000000"),
        ('0', '0', "10001111000001110000001100000001", "01101", "11100000011000000010000000000000"),
        ('0', '0', "10001111000001110000001100000001", "01110", "11000000110000000100000000000000"),
        ('0', '0', "10001111000001110000001100000001", "01111", "10000001100000001000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "10000", "00000011000000010000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "10001", "00000110000000100000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "10010", "00001100000001000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "10011", "00011000000010000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "10100", "00110000000100000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "10101", "01100000001000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "10110", "11000000010000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "10111", "10000000100000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "11000", "00000001000000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "11001", "00000010000000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "11010", "00000100000000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "11011", "00001000000000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "11100", "00010000000000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "11101", "00100000000000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "11110", "01000000000000000000000000000000"),
        ('0', '0', "10001111000001110000001100000001", "11111", "10000000000000000000000000000000"),

        -- LSR
        ('1', '0', "10001111000001110000001100000001", "00000", "10001111000001110000001100000001"),
        ('1', '0', "10001111000001110000001100000001", "00001", "01000111100000111000000110000000"),
        ('1', '0', "10001111000001110000001100000001", "00010", "00100011110000011100000011000000"),
        ('1', '0', "10001111000001110000001100000001", "00011", "00010001111000001110000001100000"),
        ('1', '0', "10001111000001110000001100000001", "00100", "00001000111100000111000000110000"),
        ('1', '0', "10001111000001110000001100000001", "00101", "00000100011110000011100000011000"),
        ('1', '0', "10001111000001110000001100000001", "00110", "00000010001111000001110000001100"),
        ('1', '0', "10001111000001110000001100000001", "00111", "00000001000111100000111000000110"),
        ('1', '0', "10001111000001110000001100000001", "01000", "00000000100011110000011100000011"),
        ('1', '0', "10001111000001110000001100000001", "01001", "00000000010001111000001110000001"),
        ('1', '0', "10001111000001110000001100000001", "01010", "00000000001000111100000111000000"),
        ('1', '0', "10001111000001110000001100000001", "01011", "00000000000100011110000011100000"),
        ('1', '0', "10001111000001110000001100000001", "01100", "00000000000010001111000001110000"),
        ('1', '0', "10001111000001110000001100000001", "01101", "00000000000001000111100000111000"),
        ('1', '0', "10001111000001110000001100000001", "01110", "00000000000000100011110000011100"),
        ('1', '0', "10001111000001110000001100000001", "01111", "00000000000000010001111000001110"),
        ('1', '0', "10001111000001110000001100000001", "10000", "00000000000000001000111100000111"),
        ('1', '0', "10001111000001110000001100000001", "10001", "00000000000000000100011110000011"),
        ('1', '0', "10001111000001110000001100000001", "10010", "00000000000000000010001111000001"),
        ('1', '0', "10001111000001110000001100000001", "10011", "00000000000000000001000111100000"),
        ('1', '0', "10001111000001110000001100000001", "10100", "00000000000000000000100011110000"),
        ('1', '0', "10001111000001110000001100000001", "10101", "00000000000000000000010001111000"),
        ('1', '0', "10001111000001110000001100000001", "10110", "00000000000000000000001000111100"),
        ('1', '0', "10001111000001110000001100000001", "10111", "00000000000000000000000100011110"),
        ('1', '0', "10001111000001110000001100000001", "11000", "00000000000000000000000010001111"),
        ('1', '0', "10001111000001110000001100000001", "11001", "00000000000000000000000001000111"),
        ('1', '0', "10001111000001110000001100000001", "11010", "00000000000000000000000000100011"),
        ('1', '0', "10001111000001110000001100000001", "11011", "00000000000000000000000000010001"),
        ('1', '0', "10001111000001110000001100000001", "11100", "00000000000000000000000000001000"),
        ('1', '0', "10001111000001110000001100000001", "11101", "00000000000000000000000000000100"),
        ('1', '0', "10001111000001110000001100000001", "11110", "00000000000000000000000000000010"),
        ('1', '0', "10001111000001110000001100000001", "11111", "00000000000000000000000000000001"),

        -- ASR
        ('1', '1', "10001111000001110000001100000001", "00000", "10001111000001110000001100000001"),
        ('1', '1', "10001111000001110000001100000001", "00001", "11000111100000111000000110000000"),
        ('1', '1', "10001111000001110000001100000001", "00010", "11100011110000011100000011000000"),
        ('1', '1', "10001111000001110000001100000001", "00011", "11110001111000001110000001100000"),
        ('1', '1', "10001111000001110000001100000001", "00100", "11111000111100000111000000110000"),
        ('1', '1', "10001111000001110000001100000001", "00101", "11111100011110000011100000011000"),
        ('1', '1', "10001111000001110000001100000001", "00110", "11111110001111000001110000001100"),
        ('1', '1', "10001111000001110000001100000001", "00111", "11111111000111100000111000000110"),
        ('1', '1', "10001111000001110000001100000001", "01000", "11111111100011110000011100000011"),
        ('1', '1', "10001111000001110000001100000001", "01001", "11111111110001111000001110000001"),
        ('1', '1', "10001111000001110000001100000001", "01010", "11111111111000111100000111000000"),
        ('1', '1', "10001111000001110000001100000001", "01011", "11111111111100011110000011100000"),
        ('1', '1', "10001111000001110000001100000001", "01100", "11111111111110001111000001110000"),
        ('1', '1', "10001111000001110000001100000001", "01101", "11111111111111000111100000111000"),
        ('1', '1', "10001111000001110000001100000001", "01110", "11111111111111100011110000011100"),
        ('1', '1', "10001111000001110000001100000001", "01111", "11111111111111110001111000001110"),
        ('1', '1', "10001111000001110000001100000001", "10000", "11111111111111111000111100000111"),
        ('1', '1', "10001111000001110000001100000001", "10001", "11111111111111111100011110000011"),
        ('1', '1', "10001111000001110000001100000001", "10010", "11111111111111111110001111000001"),
        ('1', '1', "10001111000001110000001100000001", "10011", "11111111111111111111000111100000"),
        ('1', '1', "10001111000001110000001100000001", "10100", "11111111111111111111100011110000"),
        ('1', '1', "10001111000001110000001100000001", "10101", "11111111111111111111110001111000"),
        ('1', '1', "10001111000001110000001100000001", "10110", "11111111111111111111111000111100"),
        ('1', '1', "10001111000001110000001100000001", "10111", "11111111111111111111111100011110"),
        ('1', '1', "10001111000001110000001100000001", "11000", "11111111111111111111111110001111"),
        ('1', '1', "10001111000001110000001100000001", "11001", "11111111111111111111111111000111"),
        ('1', '1', "10001111000001110000001100000001", "11010", "11111111111111111111111111100011"),
        ('1', '1', "10001111000001110000001100000001", "11011", "11111111111111111111111111110001"),
        ('1', '1', "10001111000001110000001100000001", "11100", "11111111111111111111111111111000"),
        ('1', '1', "10001111000001110000001100000001", "11101", "11111111111111111111111111111100"),
        ('1', '1', "10001111000001110000001100000001", "11110", "11111111111111111111111111111110"),
        ('1', '1', "10001111000001110000001100000001", "11111", "11111111111111111111111111111111")
      );
  begin
    -- Test all the patterns in the pattern array.
    for i in patterns'range loop
      --  Set the inputs.
      s_right <= patterns(i).right;
      s_arithmetic <= patterns(i).arithmetic;
      s_src <= patterns(i).src;
      s_shift <= patterns(i).shift;

      --  Wait for the results.
      wait for 1 ns;

      --  Check the outputs.
      s_op <= s_right & s_arithmetic;
      assert s_result = patterns(i).result
        report "Bad shift result:" & lf &
               "  op=" & to_string(s_op) & " shift=" & to_string(s_shift) & lf &
               "  src=" & to_string(s_src) & lf &
               "  res=" & to_string(s_result) & lf &
               " (exp=" & to_string(patterns(i).result) & ")"
          severity error;
    end loop;
    assert false report "End of test" severity note;
    --  Wait forever; this will finish the simulation.
    wait;
  end process;
end behavioral;

