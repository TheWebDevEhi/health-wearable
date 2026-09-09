// Thin promise wrapper around the <dialog id="password-dialog"> markup in
// index.html — kept separate from app.js so the admin-unlock flow there
// doesn't have to touch dialog plumbing directly.

const dialog = document.getElementById("password-dialog");
const form = document.getElementById("password-form");
const messageEl = document.getElementById("password-dialog-message");
const input = document.getElementById("password-input");
const cancelButton = document.getElementById("password-cancel");

// Resolves with the entered password, or null if cancelled/dismissed (Esc,
// backdrop, Cancel button all count as cancel).
export function promptPassword(message) {
    return new Promise((resolve) => {
        messageEl.textContent = message;
        input.value = "";

        function cleanup() {
            form.removeEventListener("submit", onSubmit);
            cancelButton.removeEventListener("click", onCancel);
            dialog.removeEventListener("cancel", onCancel);
        }
        function onSubmit(event) {
            event.preventDefault();
            cleanup();
            dialog.close();
            resolve(input.value);
        }
        function onCancel() {
            cleanup();
            dialog.close();
            resolve(null);
        }

        form.addEventListener("submit", onSubmit);
        cancelButton.addEventListener("click", onCancel);
        dialog.addEventListener("cancel", onCancel); // fires on Esc
        dialog.showModal();
        input.focus();
    });
}
