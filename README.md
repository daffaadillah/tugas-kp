# Kumpulan penugasan Kerja Praktik PT. Infoglobal
```mermaid
graph TD
    A[Mulai] --> B{Apakah login?}
    B -- Ya --> C[Dashboard]
    B -- Tidak --> D[Form Login]
    D --> B
