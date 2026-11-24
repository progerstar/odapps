//! The window icon: the odrfidcfgM artwork (a reader with the Modbus badge
//! and a gear), embedded as a PNG.

const ARTWORK: &[u8] = include_bytes!("../assets/icon-256.png");

pub struct Rgba {
    pub size: u32,
    pub pixels: Vec<u8>,
}

pub fn app_icon() -> Option<Rgba> {
    let mut reader = png::Decoder::new(std::io::Cursor::new(ARTWORK))
        .read_info()
        .ok()?;
    let mut buf = vec![0; reader.output_buffer_size()?];
    let info = reader.next_frame(&mut buf).ok()?;
    if info.color_type != png::ColorType::Rgba || info.bit_depth != png::BitDepth::Eight {
        return None;
    }
    buf.truncate(info.buffer_size());
    (info.width == info.height).then_some(Rgba {
        size: info.width,
        pixels: buf,
    })
}

#[cfg(test)]
mod tests {
    #[test]
    fn decodes() {
        let icon = super::app_icon().expect("embedded icon");
        assert_eq!(icon.size, 256);
        assert_eq!(icon.pixels.len(), 256 * 256 * 4);
        assert_eq!(icon.pixels[3], 0, "corners are transparent");
    }
}
