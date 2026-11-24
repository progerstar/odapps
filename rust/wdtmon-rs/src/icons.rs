//! Window and tray icons drawn from the wdtmon3-mini artwork: a dog head with
//! the USB trident, black on a white disc. The rendered icons put a white dog
//! on a disc in the state colour, which reads on dark and light panels alike.

use std::sync::OnceLock;

use crate::engine::Health;

const ARTWORK: &[u8] = include_bytes!("../assets/icon-512.png");

pub struct Rgba {
    pub size: u32,
    pub pixels: Vec<u8>,
}

/// Coverage of the dog (0..=1) in the source image; the disc's outline ring
/// is left out.
fn coverage() -> &'static (u32, Vec<f32>) {
    static MASK: OnceLock<(u32, Vec<f32>)> = OnceLock::new();
    MASK.get_or_init(|| {
        let mut reader = png::Decoder::new(std::io::Cursor::new(ARTWORK))
            .read_info()
            .expect("embedded icon is a PNG");
        let mut buf = vec![0; reader.output_buffer_size().expect("icon size")];
        let info = reader.next_frame(&mut buf).expect("embedded icon decodes");
        let (w, h) = (info.width, info.height);
        let channels = info.color_type.samples();
        let size = w.min(h);
        let half = size as f32 / 2.0;
        let mut mask = vec![0.0f32; (size * size) as usize];
        for y in 0..size {
            for x in 0..size {
                let px = &buf[((y * w + x) as usize) * channels..][..channels];
                let (luma, alpha) = match channels {
                    4 => (
                        (px[0] as f32 + px[1] as f32 + px[2] as f32) / 3.0,
                        px[3] as f32,
                    ),
                    3 => ((px[0] as f32 + px[1] as f32 + px[2] as f32) / 3.0, 255.0),
                    2 => (px[0] as f32, px[1] as f32),
                    _ => (px[0] as f32, 255.0),
                };
                let d = ((x as f32 + 0.5 - half).powi(2) + (y as f32 + 0.5 - half).powi(2)).sqrt();
                if d < half * 0.94 {
                    mask[(y * size + x) as usize] = (1.0 - luma / 255.0) * alpha / 255.0;
                }
            }
        }
        (size, mask)
    })
}

/// A disc in `color` with the dog in white, `size`×`size`.
pub fn render(size: u32, color: [u8; 3]) -> Rgba {
    let (src, mask) = coverage();
    let scale = *src as f32 / size as f32;
    let r = size as f32 / 2.0;
    let mut pixels = vec![0u8; (size * size * 4) as usize];
    for y in 0..size {
        for x in 0..size {
            // Box filter over the source pixels under this one.
            let (x0, y0) = ((x as f32 * scale) as u32, (y as f32 * scale) as u32);
            let x1 = (((x + 1) as f32 * scale) as u32).clamp(x0 + 1, *src);
            let y1 = (((y + 1) as f32 * scale) as u32).clamp(y0 + 1, *src);
            let mut sum = 0.0;
            for sy in y0..y1 {
                for sx in x0..x1 {
                    sum += mask[(sy * src + sx) as usize];
                }
            }
            let white = (sum / ((x1 - x0) * (y1 - y0)) as f32).clamp(0.0, 1.0);
            let d = ((x as f32 + 0.5 - r).powi(2) + (y as f32 + 0.5 - r).powi(2)).sqrt();
            let disc = (r - 0.5 - d).clamp(0.0, 1.0);
            let i = ((y * size + x) * 4) as usize;
            for c in 0..3 {
                pixels[i + c] = (color[c] as f32 * (1.0 - white) + 255.0 * white).round() as u8;
            }
            pixels[i + 3] = (disc * 255.0).round() as u8;
        }
    }
    Rgba { size, pixels }
}

pub fn state_color(health: Health) -> [u8; 3] {
    match health {
        Health::Ok => [46, 160, 90],
        Health::Paused => [230, 140, 20],
        Health::Problem => [214, 58, 58],
        Health::Unknown => [120, 128, 140],
    }
}

pub fn tray_icon(health: Health, size: u32) -> Rgba {
    render(size, state_color(health))
}

/// The window icon in the Open Development blue.
pub fn app_icon() -> Rgba {
    render(128, [0, 154, 223])
}

impl Rgba {
    /// ARGB32, network byte order (StatusNotifierItem).
    #[cfg_attr(not(target_os = "linux"), allow(dead_code))]
    pub fn to_argb(&self) -> Vec<u8> {
        self.pixels
            .as_chunks::<4>()
            .0
            .iter()
            .flat_map(|p| [p[3], p[0], p[1], p[2]])
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dog_on_disc() {
        let icon = tray_icon(Health::Ok, 32);
        assert_eq!(icon.pixels.len(), 32 * 32 * 4);
        assert_eq!(icon.pixels[3], 0, "corners are transparent");
        let px: Vec<&[u8; 4]> = icon
            .pixels
            .as_chunks::<4>()
            .0
            .iter()
            .filter(|p| p[3] == 255)
            .collect();
        let white = px.iter().filter(|p| p[0] > 240 && p[1] > 240).count();
        let green = px
            .iter()
            .filter(|p| p[..3] == state_color(Health::Ok))
            .count();
        assert!(white > 60, "{white}");
        assert!(green > 60, "{green}");
        assert_eq!(icon.to_argb().len(), icon.pixels.len());
    }
}
