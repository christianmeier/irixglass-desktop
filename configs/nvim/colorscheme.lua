-- Nord theme, matching Alacritty and tmux
return {
  {
    "gbprod/nord.nvim",
    lazy = false,
    priority = 1000,
    opts = {},
  },
  {
    "LazyVim/LazyVim",
    opts = {
      colorscheme = "nord",
    },
  },
}
