class MagiaArch:
    # Single tile address map from magia_tile_pkg.sv
    RESERVED_ADDR_START = 0x0000_0000
    RESERVED_SIZE       = 0x0000_FFFF
    RESERVED_ADDR_END   = RESERVED_ADDR_START + RESERVED_SIZE
    STACK_ADDR_START    = RESERVED_ADDR_END + 1
    STACK_SIZE          = 0x0000_FFFF
    STACK_ADDR_END      = STACK_ADDR_START + STACK_SIZE
    L1_ADDR_START       = STACK_ADDR_END + 1
    L1_SIZE             = 0x000D_FFFF
    L1_ADDR_END         = L1_ADDR_START + L1_SIZE
    L1_TILE_OFFSET      = 0x0010_0000
    L2_ADDR_START       = 0xC000_0000
    L2_SIZE             = 0x4000_0000
    L2_ADDR_END         = L2_ADDR_START + L2_SIZE
    STDOUT_START        = 0xFFFF_0004
    STDOUT_SIZE         = 0x100

    # From magia_pkg.sv
    N_MEM_BANKS         = 32        # Number of TCDM banks
    N_WORDS_BANK        = 8192      # Number of words per TCDM bank

    # Extra
    BYTES_PER_WORD      = 4
    TILE_CLK_FREQ       = 50 * (10 ** 6)

    ENABLE_NOC          = False
    N_TILES_X           = 4
    N_TILES_Y           = 4
    NB_CLUSTERS         = N_TILES_X*N_TILES_Y