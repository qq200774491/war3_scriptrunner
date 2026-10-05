use anyhow::{bail, Result};
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

pub const MAX_FRAME: usize = 1024 * 1024;

#[derive(Debug)]
pub enum Message {
    Log {
        level: u32,
        text: String,
    },
    Event {
        player: i32,
        name: String,
        payload: Vec<u8>,
    },
    Ready,
    Ack(u64),
    Failure(String),
    Heartbeat,
    Global {
        transaction: u32,
        operation: String,
        key: String,
        argument: String,
    },
}

struct Cursor<'a>(&'a [u8]);
impl<'a> Cursor<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8]> {
        anyhow::ensure!(n <= self.0.len(), "truncated host frame");
        let (left, right) = self.0.split_at(n);
        self.0 = right;
        Ok(left)
    }
    fn u32(&mut self) -> Result<u32> {
        Ok(u32::from_le_bytes(self.take(4)?.try_into()?))
    }
    fn blob(&mut self) -> Result<&'a [u8]> {
        let n = self.u32()? as usize;
        self.take(n)
    }
    fn text(&mut self) -> Result<String> {
        Ok(String::from_utf8(self.blob()?.to_vec())?)
    }
}

pub fn decode(bytes: &[u8]) -> Result<Message> {
    let mut c = Cursor(bytes);
    let kind = c.take(1)?[0];
    let message = match kind {
        1 => Message::Log {
            level: c.u32()?,
            text: String::from_utf8_lossy(c.blob()?).into_owned(),
        },
        2 => Message::Event {
            player: c.u32()? as i32,
            name: c.text()?,
            payload: c.blob()?.to_vec(),
        },
        3 => {
            anyhow::ensure!(c.u32()? == 1, "invalid host readiness");
            Message::Ready
        }
        4 => Message::Ack(u64::from_le_bytes(c.take(8)?.try_into()?)),
        5 => Message::Failure(c.text()?),
        6 => Message::Heartbeat,
        7 => Message::Global {
            transaction: c.u32()?,
            operation: c.text()?,
            key: c.text()?,
            argument: c.text()?,
        },
        _ => bail!("unknown host frame type {kind}"),
    };
    anyhow::ensure!(c.0.is_empty(), "trailing host bytes");
    Ok(message)
}

pub async fn read(reader: &mut (impl AsyncRead + Unpin)) -> Result<Message> {
    let size = reader.read_u32_le().await? as usize;
    anyhow::ensure!((1..=MAX_FRAME).contains(&size), "invalid host frame length");
    let mut bytes = vec![0; size];
    reader.read_exact(&mut bytes).await?;
    decode(&bytes)
}

pub async fn write(writer: &mut (impl AsyncWrite + Unpin), body: &[u8]) -> Result<()> {
    anyhow::ensure!(
        !body.is_empty() && body.len() <= MAX_FRAME,
        "invalid outgoing host frame"
    );
    writer.write_u32_le(body.len() as u32).await?;
    writer.write_all(body).await?;
    writer.flush().await?;
    Ok(())
}

pub fn event(kind: u8, sequence: u64, player: i32, name: &str, payload: &str) -> Result<Vec<u8>> {
    let mut bytes = vec![kind];
    bytes.extend(sequence.to_le_bytes());
    bytes.extend(player.to_le_bytes());
    for text in [name, payload] {
        anyhow::ensure!(
            !text.contains('\0') && text.len() <= MAX_FRAME,
            "invalid host text"
        );
        bytes.extend((text.len() as u32).to_le_bytes());
        bytes.extend(text.as_bytes());
    }
    anyhow::ensure!(bytes.len() <= MAX_FRAME, "host frame exceeds limit");
    Ok(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_invalid_frames() {
        for bytes in [
            &[][..],
            &[3, 1, 0, 0][..],
            &[3, 2, 0, 0, 0][..],
            &[6, 0][..],
            &[99][..],
        ] {
            assert!(decode(bytes).is_err());
        }
    }
    #[test]
    fn event_preserves_player_and_unicode() {
        let bytes = event(0x10, 43, -1, "名称", "存档").unwrap();
        assert_eq!(&bytes[1..9], &43u64.to_le_bytes());
        assert_eq!(&bytes[9..13], &(-1i32).to_le_bytes());
        assert!(event(0x10, 1, 0, "bad\0", "").is_err());
    }
}
