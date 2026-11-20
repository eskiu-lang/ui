package org.eskiu.ui;

import android.os.Build;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import java.util.HashMap;

// A text input's editing options (props 65 to 72): the input type (secure, keyboard,
// multiline, capitalize, autocorrect), the IME action, autofill hints and submit on return.
final class UiInput {
    private final UiActivity activity;
    UiInput(UiActivity activity) { this.activity = activity; }

    // Per field: secure, keyboard, return key, multiline, capitalize, autocorrect, autofill,
    // submit (props 65 to 72, in order).
    private final HashMap<Integer, int[]> options = new HashMap<>();

    void forget(int id) { options.remove(id); }
    void clear() { options.clear(); }

    // An int prop from 65 to 72 on a text input.
    void prop(int id, EditText field, int code, int value) {
        int[] o = options.get(id);
        if (o == null) {
            o = new int[] {0, 0, 0, 0, 0, 1, 0, 0};
            options.put(id, o);
            final int[] mine = o;
            field.setOnEditorActionListener((v, action, event) -> submit(id, field, mine, event));
        }
        o[code - 65] = value;
        if (code == 72) return;
        if (code == 71) { autofill(field, value); return; }
        activity.applying = true;
        if (code == 67) field.setImeOptions(imeAction(o));
        else {
            field.setInputType(inputType(o));
            field.setImeOptions(imeAction(o));
            // A password type sets a monospace face: the node's own font again.
            activity.props.refreshFont(id, field);
        }
        activity.applying = false;
        if (field.isFocused()) {
            android.view.inputmethod.InputMethodManager keyboard =
                (android.view.inputmethod.InputMethodManager)activity.getSystemService(android.content.Context.INPUT_METHOD_SERVICE);
            if (keyboard != null) keyboard.restartInput(field);
        }
    }

    private static int inputType(int[] o) {
        boolean secure = o[0] != 0, multiline = o[3] != 0;
        int keyboard = o[1];
        if (keyboard == 2 || keyboard == 3) {
            int type = InputType.TYPE_CLASS_NUMBER;
            if (keyboard == 3) type |= InputType.TYPE_NUMBER_FLAG_DECIMAL;
            if (secure) type |= InputType.TYPE_NUMBER_VARIATION_PASSWORD;
            return type;
        }
        if (keyboard == 4) return secure ? InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_VARIATION_PASSWORD : InputType.TYPE_CLASS_PHONE;
        int type = InputType.TYPE_CLASS_TEXT;
        if (secure) type |= InputType.TYPE_TEXT_VARIATION_PASSWORD;
        else if (keyboard == 1) type |= InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS;
        else if (keyboard == 5) type |= InputType.TYPE_TEXT_VARIATION_URI;
        // A password is one line.
        if (multiline && !secure) type |= InputType.TYPE_TEXT_FLAG_MULTI_LINE;
        int capitalize = o[4];
        if (capitalize == 2) type |= InputType.TYPE_TEXT_FLAG_CAP_WORDS;
        else if (capitalize == 3) type |= InputType.TYPE_TEXT_FLAG_CAP_SENTENCES;
        else if (capitalize == 4) type |= InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS;
        if (o[5] == 0) type |= InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        else if (!secure) type |= InputType.TYPE_TEXT_FLAG_AUTO_CORRECT;
        return type;
    }
    private static int imeAction(int[] o) {
        final int[] actions = {EditorInfo.IME_ACTION_UNSPECIFIED, EditorInfo.IME_ACTION_DONE, EditorInfo.IME_ACTION_GO,
            EditorInfo.IME_ACTION_NEXT, EditorInfo.IME_ACTION_SEARCH, EditorInfo.IME_ACTION_SEND};
        int returnKey = o[2] >= 0 && o[2] < actions.length ? o[2] : 0;
        // A search keyboard's return key searches unless the app chose another.
        if (returnKey == 0 && o[1] == 6) return EditorInfo.IME_ACTION_SEARCH;
        return actions[returnKey];
    }
    private static void autofill(EditText field, int value) {
        if (Build.VERSION.SDK_INT < 26) return;
        // androidx's HintConstants spell the new password and the one-time code this way.
        final String[] hints = {null, View.AUTOFILL_HINT_USERNAME, View.AUTOFILL_HINT_PASSWORD, "newPassword",
            View.AUTOFILL_HINT_EMAIL_ADDRESS, "smsOTPCode", View.AUTOFILL_HINT_NAME, View.AUTOFILL_HINT_PHONE};
        String hint = value > 0 && value < hints.length ? hints[value] : null;
        if (hint == null) field.setAutofillHints((String[])null);
        else field.setAutofillHints(hint);
    }
    // Return in a one-line field: the app's submit, with the text as it is now. A hardware
    // return comes down and up: the down submits, the up is consumed.
    private boolean submit(int id, EditText field, int[] o, KeyEvent event) {
        int type = field.getInputType();
        boolean multiline = (type & InputType.TYPE_MASK_CLASS) == InputType.TYPE_CLASS_TEXT && (type & InputType.TYPE_TEXT_FLAG_MULTI_LINE) != 0;
        if (o[7] == 0 || multiline) return false;
        if (event != null && event.getAction() != KeyEvent.ACTION_DOWN) return true;
        if (activity.active && activity.generations.containsKey(id))
            activity.nativeSubmit(id, activity.generations.get(id), field.getText().toString());
        // A soft keyboard's action still does its own part (done hides it, next moves on).
        return event != null;
    }
}
