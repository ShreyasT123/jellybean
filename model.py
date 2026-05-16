import torch
import torch.nn as nn
import torch.nn.functional as F



# =========================================================
# SwiGLU
# =========================================================
class SwiGLU(nn.Module):
    def __init__(
        self,
        dim: int,
        hidden_dim: int
    ):
        super().__init__()

        self.w1 = nn.Linear(
            dim,
            hidden_dim,
            bias=False
        )

        self.w2 = nn.Linear(
            dim,
            hidden_dim,
            bias=False
        )

        self.w3 = nn.Linear(
            hidden_dim,
            dim,
            bias=False
        )

    def forward(
        self,
        x: torch.Tensor
    ) -> torch.Tensor:

        gate = F.silu(self.w1(x))
        value = self.w2(x)

        return self.w3(
            gate * value
        )


# =========================================================
# Multi-Head Self Attention
#
# Uses optimized SDPA
# Export-safe for LibTorch
# =========================================================
class MultiHeadSelfAttention(nn.Module):
    def __init__(
        self,
        dim: int,
        num_heads: int,
        dropout: float = 0.0
    ):
        super().__init__()

        assert dim % num_heads == 0

        self.dim = dim
        self.num_heads = num_heads
        self.head_dim = dim // num_heads
        self.dropout = dropout

        self.qkv = nn.Linear(
            dim,
            dim * 3,
            bias=False
        )

        self.out_proj = nn.Linear(
            dim,
            dim,
            bias=False
        )

    def forward(
        self,
        x: torch.Tensor
    ) -> torch.Tensor:

        B, S, E = x.shape

        qkv = self.qkv(x)

        q, k, v = torch.chunk(
            qkv,
            3,
            dim=-1
        )

        # [B, S, E]
        # -> [B, H, S, D]

        q = q.view(
            B,
            S,
            self.num_heads,
            self.head_dim
        ).transpose(1, 2)

        k = k.view(
            B,
            S,
            self.num_heads,
            self.head_dim
        ).transpose(1, 2)

        v = v.view(
            B,
            S,
            self.num_heads,
            self.head_dim
        ).transpose(1, 2)

        # -------------------------------------------------
        # Optimized SDPA
        # PyTorch picks:
        # - Flash Attention
        # - Efficient Attention
        # - Math fallback
        # automatically
        # -------------------------------------------------

        out = F.scaled_dot_product_attention(
            q,
            k,
            v,
            attn_mask=None,
            dropout_p=self.dropout
            if self.training else 0.0,
            is_causal=True
        )

        # [B, H, S, D]
        # -> [B, S, E]

        out = out.transpose(
            1,
            2
        ).contiguous()

        out = out.view(
            B,
            S,
            E
        )

        return self.out_proj(out)


# =========================================================
# Decoder Block
#
# Pre-Norm RMSNorm Transformer
# =========================================================
class DecoderBlock(nn.Module):
    def __init__(
        self,
        dim: int,
        num_heads: int,
        ff_hidden_dim: int
    ):
        super().__init__()

        self.norm1 = nn.RMSNorm(dim)

        self.attn = MultiHeadSelfAttention(
            dim,
            num_heads
        )

        self.norm2 = nn.RMSNorm(dim)

        self.ffn = SwiGLU(
            dim,
            ff_hidden_dim
        )

    def forward(
        self,
        x: torch.Tensor
    ) -> torch.Tensor:

        # Attention residual
        x = x + self.attn(
            self.norm1(x)
        )

        # FFN residual
        x = x + self.ffn(
            self.norm2(x)
        )

        return x


# =========================================================
# Decoder Transformer
#
# Input : [B, S, E]
# Output: [B, E]
# =========================================================
class DecoderTransformer(nn.Module):
    def __init__(
        self,
        dim: int = 512,
        num_heads: int = 8,
        ff_hidden_dim: int = 2048,
        num_layers: int = 8
    ):
        super().__init__()

        self.blocks = nn.ModuleList([
            DecoderBlock(
                dim,
                num_heads,
                ff_hidden_dim
            )
            for _ in range(num_layers)
        ])

        self.final_norm = nn.RMSNorm(dim)

    def forward(
        self,
        x: torch.Tensor
    ) -> torch.Tensor:

        for block in self.blocks:
            x = block(x)

        x = self.final_norm(x)

        # Last token pooling
        return x[:, -1]


# =========================================================
# Export Example
# =========================================================
if __name__ == "__main__":

    device = (
        "cuda"
        if torch.cuda.is_available()
        else "cpu"
    )

    model = DecoderTransformer(
        dim=512,
        num_heads=8,
        ff_hidden_dim=2048,
        num_layers=8
    ).to(device)

    model.eval()

    x = torch.randn(
        1,
        128,
        512,
        device=device
    )

    y = model(x)

    print("Output shape:", y.shape)

    # =====================================================
    # TorchScript Export
    # =====================================================

    scripted_model = torch.jit.script(model)

    scripted_model.save(
        "model.pt"
    )

    print("Model exported.")
