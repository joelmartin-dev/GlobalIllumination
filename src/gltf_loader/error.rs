use std::{error::Error, fmt::Display};

#[derive(Debug)]
pub struct GltfError {
    details: String
}
impl From<&str> for GltfError {
    fn from(value: &str) -> Self {
        Self { details: String::from(value) }
    }
}
impl From<String> for GltfError {
    fn from(value: String) -> Self {
        Self { details: value }
    }
}
impl Display for GltfError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.details)
    }
}
impl Error for GltfError {}