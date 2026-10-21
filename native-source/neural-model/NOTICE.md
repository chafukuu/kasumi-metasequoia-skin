# chinese-ime-lm sentence models

The two neural sentence models shipped here are from the **chinese-ime-lm** project
(<https://github.com/metasequoiaime/chinese-ime-lm>), release `model-v1`:

- `sentence-model.safetensors` — "keyboard" preset (vocab 8192, 6 layers, n_embd 192, context 64).
- `sentence-model-desktop.safetensors` — "desktop" preset (vocab 12288, 8 layers, n_embd 448, context 128).

Both are character-level Transformer language models used to decode pinyin into whole sentences.

## License

The model **weights** are licensed under **Apache-2.0** (recorded in each file's
`__metadata__["license"]`). The C++ inference code in `engine/neural/` is a port of the project's
Apache-2.0 reference implementation (`reference/`).

## Attribution

Trained on the Chinese portion of C4 (ODC-BY, <https://huggingface.co/datasets/allenai/c4>) and
LCCC (MIT, <https://github.com/thu-coai/CDial-GPT>).
