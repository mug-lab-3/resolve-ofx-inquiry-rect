# DaVinci Resolve 挙動確認用プラグイン

## 目的

このプラグインは、DaVinci Resolveの不審な挙動を再現・可視化します。
詳細は [InquiryPlugin.cpp](InquiryPlugin.cpp) の先頭コメントに記載しています。

1. **in/out 区間を設定してジェネレータークリップを配置した場合、 `render()` 常に `time = 0` が渡される。**
   四角形は `RenderArguments::time` のみからサイズが決まるため、通常はクリップの
   再生に伴ってサイズが変化します。しかし in/out 点を設定し、その区間内にクリップを
   配置すると、再生してもサイズが変化しません。これは `render()` に常に `time = 0` が
   渡され、クリップの実際のタイムラインフレームが渡ってこないためです。

2. **カット後、プラグイン側からキーフレームを設定すると、誤った位置にキーフレームが設定される。**
   クリップを2つにカットした後、*2番目* のクリップでボタンを押し、
   `setValueAtTime(0.0, 1.0)` でフレーム0にキーフレームを設定します。
   本来は2番目のクリップ自身のフレーム0にキーフレームが設定されるべきですが、
   実際には1番目(分割前)のクリップのフレーム0の位置にキーフレームが設定されてしまいます。

## ビルド方法

依存しているのはDaVinci Resolve付属のSDK(OpenFX-1.4 ヘッダ + Support ソース)のみです。
OpenFX サンプルの `GainPlugin` と全く同じやり方でビルドできます。すなわち
`InquiryPlugin.cpp` を OFX Support ライブラリのソースと一緒にコンパイルするだけです。

## インストール(Windows)

`InquiryPlugin.ofx.bundle` ディレクトリごと、以下にコピーします:

```
C:\Program Files\Common Files\OFX\Plugins
```

"Resolve Inquiry" グループの
**Inquiry Rect** として、ジェネレータ／エフェクト両方のリストに表示されます。
